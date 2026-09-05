//! Generate-time gate: a division whose divisor is a *running sum* must be
//! guarded on that sum itself, not on a proxy for it.
//!
//! A window sum is maintained by add-then-subtract, so its value is not a
//! function of the window's contents alone -- absorption can drive it to
//! exactly 0.0 while the window still holds a live term, and a subtract at
//! full precision can take it to 0.0 or below on a window that is not flat.
//! Every predicate that reasons about the window instead (a flat-bar counter,
//! a comparison against the numerator, a period clamp) is therefore only a
//! proxy: correct in exact arithmetic, false in floating point, and the
//! division it gates emits NaN or +/-Inf on a legal input series.
//!
//! The rule is one sentence: **if the divisor is a running sum, some enclosing
//! condition must name that same sum against zero.** Direction is not checked --
//! `> 0.0`, `<= 0.0`, `TA_IS_ZERO(s)` and `!TA_IS_ZERO(s)` all satisfy it, because
//! which arm divides is the author's business and which *variable* is tested is
//! not. A proxy predicate names something else and is what fires here.
//!
//! Scope is deliberately the narrow case that has actually shipped defects: the
//! divisor is a bare `double` local that some statement accumulates into. A
//! divisor that is an expression, a parameter, or an array element is not
//! analyzed -- a guard gate that reports maybes is a gate people learn to skip.
//! Everything here is biased the same way: the alias hop and the repair form
//! below only ever EXCUSE a division, so a widening makes the gate quieter, and
//! a narrowing is what would make it wrong.

use crate::ir::{BinOp, Expr, FuncDef, Statement, VarType};
use std::collections::HashSet;

/// One unguarded division, in the terms the author needs to fix it.
struct Finding {
    func: String,
    divisor: String,
    /// The conditions that *do* enclose the site, for the message. Empty when
    /// the division is unconditional.
    seen: Vec<String>,
}

impl Finding {
    fn render(&self) -> String {
        let seen = if self.seen.is_empty() {
            "no enclosing condition".to_string()
        } else {
            format!("enclosing conditions test {}", self.seen.join(", "))
        };
        format!(
            "{}: divides by the running sum `{}`, which no enclosing condition tests against zero ({seen}). \
             A window sum reaches exactly 0.0 by floating-point absorption on inputs its bar-counting \
             proxies call non-flat; gate the division on `{}` itself.",
            self.func, self.divisor, self.divisor
        )
    }
}

/// Run the gate over every function definition.
///
/// # Errors
/// One message per unguarded division, ready to print.
pub fn validate_all(defs: &[FuncDef]) -> Result<(), Vec<String>> {
    let mut findings = Vec::new();
    for def in defs {
        check_body(&def.name, &def.body, &mut findings);
        if def.has_explicit_private {
            check_body(&def.name, &def.private_body, &mut findings);
        }
        for alt in &def.alternates {
            check_body(&def.name, &alt.body, &mut findings);
        }
    }
    if findings.is_empty() {
        return Ok(());
    }
    // A body reached twice (an alternate that re-states the same loop) would
    // otherwise report the same site twice.
    let mut seen = HashSet::new();
    let mut msgs = Vec::new();
    for f in &findings {
        let msg = f.render();
        if seen.insert(msg.clone()) {
            msgs.push(msg);
        }
    }
    Err(msgs)
}

fn check_body(func: &str, body: &[Statement], findings: &mut Vec<Finding>) {
    let sums = running_sums(body);
    if sums.is_empty() {
        return;
    }
    let alias = aliases(body);
    let mut ctx = Ctx {
        func,
        sums: &sums,
        alias: &alias,
        findings,
    };
    ctx.walk(body, &Guards::default());
}

/// The variables an enclosing condition has tested against zero, plus the
/// spelling of every condition on the way in (for the message).
#[derive(Default, Clone)]
struct Guards {
    tested: HashSet<String>,
    seen: Vec<String>,
}

impl Guards {
    fn with(&self, cond: &Expr) -> Self {
        let mut next = self.clone();
        zero_tested(cond, &mut next.tested);
        next.seen.push(describe(cond));
        next
    }
}

struct Ctx<'a> {
    func: &'a str,
    sums: &'a HashSet<String>,
    alias: &'a HashSet<(String, String)>,
    findings: &'a mut Vec<Finding>,
}

impl Ctx<'_> {
    /// The divisor is guarded when some enclosing zero test names it, or names
    /// a variable that reads it.
    fn is_guarded(&self, divisor: &str, guards: &Guards) -> bool {
        guards.tested.iter().any(|tested| {
            tested == divisor || self.alias.contains(&(tested.clone(), divisor.to_string()))
        })
    }

    fn walk(&mut self, stmts: &[Statement], guards: &Guards) {
        // A `if( bad ) return;` earlier in the block guards everything after it
        // just as an enclosing `if` guards its own body, so the running set
        // grows as the block is walked.
        let mut here = guards.clone();
        for stmt in stmts {
            self.walk_stmt(stmt, &here);
            if let Statement::If {
                condition,
                then_body,
                else_body,
                ..
            } = stmt
            {
                if else_body.is_empty() && (diverges(then_body) || repairs(condition, then_body)) {
                    here = here.with(condition);
                }
            }
        }
    }

    fn walk_stmt(&mut self, stmt: &Statement, guards: &Guards) {
        match stmt {
            Statement::If {
                condition,
                then_body,
                else_body,
                ..
            } => {
                self.expr(condition, guards);
                let inner = guards.with(condition);
                self.walk(then_body, &inner);
                self.walk(else_body, &inner);
            }
            Statement::While { condition, body } | Statement::DoWhile { condition, body } => {
                self.expr(condition, guards);
                self.walk(body, &guards.with(condition));
            }
            Statement::ForC {
                init,
                condition,
                update,
                body,
            } => {
                self.walk_stmt(init, guards);
                self.expr(condition, guards);
                let inner = guards.with(condition);
                self.walk_stmt(update, &inner);
                self.walk(body, &inner);
            }
            Statement::For { count, body, .. } => {
                self.expr(count, guards);
                self.walk(body, guards);
            }
            Statement::Switch {
                expr,
                cases,
                default,
            } => {
                self.expr(expr, guards);
                for (_, body) in cases {
                    self.walk(body, guards);
                }
                self.walk(default, guards);
            }
            Statement::Block { body } => self.walk(body, guards),
            Statement::Assign { target, value, .. } => {
                self.expr(target, guards);
                self.expr(value, guards);
            }
            Statement::VarDecl { init: Some(e), .. } | Statement::Return { value: Some(e) } => {
                self.expr(e, guards);
            }
            Statement::Expr(e) => self.expr(e, guards),
            _ => {}
        }
    }

    fn expr(&mut self, e: &Expr, guards: &Guards) {
        match e {
            Expr::BinOp(lhs, op, rhs) => {
                if *op == BinOp::Div {
                    if let Expr::Var(name) = rhs.as_ref() {
                        if self.sums.contains(name) && !self.is_guarded(name, guards) {
                            self.findings.push(Finding {
                                func: self.func.to_string(),
                                divisor: name.clone(),
                                seen: guards.seen.clone(),
                            });
                        }
                    }
                }
                self.expr(lhs, guards);
                self.expr(rhs, guards);
            }
            // A ternary's condition guards both arms, exactly as an `if` does.
            Expr::Ternary(cond, a, b) => {
                self.expr(cond, guards);
                let inner = guards.with(cond);
                self.expr(a, &inner);
                self.expr(b, &inner);
            }
            Expr::ArrayAccess(_, idx) => self.expr(idx, guards),
            Expr::Cast(_, inner)
            | Expr::Not(inner)
            | Expr::BitwiseNot(inner)
            | Expr::AddressOf(inner)
            | Expr::PostIncrement(inner)
            | Expr::PostDecrement(inner)
            | Expr::PreIncrement(inner)
            | Expr::PreDecrement(inner) => self.expr(inner, guards),
            Expr::FuncCall(_, args) => {
                for a in args {
                    self.expr(a, guards);
                }
            }
            _ => {}
        }
    }
}

/// True when the branch does nothing but write the name its own condition
/// tested against zero: `if( s <= 0.0 ) s = 1.0;` answers the domain question
/// by repair instead of by branch, and the divisions after it are guarded.
fn repairs(condition: &Expr, body: &[Statement]) -> bool {
    let mut tested = HashSet::new();
    zero_tested(condition, &mut tested);
    !body.is_empty()
        && body.iter().all(
            |s| matches!(s, Statement::Assign { target: Expr::Var(v), .. } if tested.contains(v)),
        )
}

/// True when every path out of `body` leaves the enclosing block -- which is
/// what makes an early-out `if` a guard over the statements that follow it.
fn diverges(body: &[Statement]) -> bool {
    matches!(
        body.last(),
        Some(Statement::Return { .. } | Statement::Break | Statement::Continue)
    )
}

/// Scalars that hold nothing but a running sum, plus the plain copies of one.
///
/// "Nothing but" is the whole precision of this gate. The analysis is
/// flow-insensitive, so a scratch name that is accumulated into *somewhere* and
/// reloaded from an input array *elsewhere* (`tempReal` is the corpus's, in every
/// Hilbert-transform function) would otherwise taint every division it ever
/// appears under. Requiring EVERY assignment to be an accumulation, a constant,
/// or a copy of another such name leaves the scratch out and costs nothing real:
/// a window sum that is also used as a scratch register is not a shape any
/// indicator here has.
///
/// The copy hop is not a convenience either: the shape that motivated this gate
/// records the sum into a `cur` local and divides by *that*, so a set without it
/// analyzes nothing.
fn running_sums(body: &[Statement]) -> HashSet<String> {
    let mut assigns = Vec::new();
    collect_assigns(body, &mut assigns);

    // Only `double` locals. A parameter is the caller's value and an `int` is a
    // count or an index: neither is a sum this rule is about, and MACD's
    // period swap would otherwise read as one.
    let mut locals = HashSet::new();
    collect_real_locals(body, &mut locals);

    let mut targets: HashSet<&str> = HashSet::new();
    for (target, _) in &assigns {
        if locals.contains(target.as_str()) {
            targets.insert(target.as_str());
        }
    }

    // Start from "every candidate qualifies" and remove the ones an assignment
    // disqualifies, re-checking until the copy edges settle: a copy of a name
    // that has just been ruled out is ruled out too.
    let mut sums: HashSet<String> = targets.iter().map(|s| (*s).to_string()).collect();
    loop {
        let mut drop_now: Option<String> = None;
        for (target, value) in &assigns {
            if !sums.contains(target) {
                continue;
            }
            if !holds_a_sum(target, value, &sums) {
                drop_now = Some(target.clone());
                break;
            }
        }
        match drop_now {
            Some(name) => {
                sums.remove(&name);
            }
            None => break,
        }
    }
    // A name only ever assigned a constant is not a sum, however it is used.
    sums.retain(|name| {
        assigns
            .iter()
            .any(|(t, v)| t == name && !matches!(v, Expr::Literal(_) | Expr::IntLiteral(_)))
    });
    sums
}

/// True when this one assignment leaves `target` holding a running sum: it
/// accumulates into itself, resets to a constant, or copies a name that does.
fn holds_a_sum(target: &str, value: &Expr, sums: &HashSet<String>) -> bool {
    match value {
        Expr::Literal(_) | Expr::IntLiteral(_) => true,
        Expr::Var(src) => sums.contains(src),
        Expr::BinOp(lhs, BinOp::Add | BinOp::Sub, rhs) => {
            matches!(lhs.as_ref(), Expr::Var(v) if v == target)
                || matches!(rhs.as_ref(), Expr::Var(v) if v == target)
        }
        _ => false,
    }
}

/// For each name, the names any assignment to it reads. One hop, and used only
/// to EXCUSE a division: a guard on `tempReal` where `tempReal = fabs(sum)` is a
/// guard on the sum, and reading the alias in that direction can only make the
/// gate quieter, never noisier.
fn aliases(body: &[Statement]) -> HashSet<(String, String)> {
    let mut assigns = Vec::new();
    collect_assigns(body, &mut assigns);
    let mut out = HashSet::new();
    for (target, value) in &assigns {
        let mut read = HashSet::new();
        vars_in(value, &mut read);
        for src in read {
            out.insert((target.clone(), src));
        }
    }
    out
}

fn vars_in(e: &Expr, out: &mut HashSet<String>) {
    match e {
        Expr::Var(v) => {
            out.insert(v.clone());
        }
        Expr::BinOp(a, _, b) => {
            vars_in(a, out);
            vars_in(b, out);
        }
        Expr::Ternary(a, b, c) => {
            vars_in(a, out);
            vars_in(b, out);
            vars_in(c, out);
        }
        Expr::Cast(_, i)
        | Expr::Not(i)
        | Expr::BitwiseNot(i)
        | Expr::AddressOf(i)
        | Expr::PostIncrement(i)
        | Expr::PostDecrement(i)
        | Expr::PreIncrement(i)
        | Expr::PreDecrement(i) => vars_in(i, out),
        Expr::ArrayAccess(_, idx) => vars_in(idx, out),
        Expr::FuncCall(_, args) => {
            for a in args {
                vars_in(a, out);
            }
        }
        _ => {}
    }
}

fn collect_real_locals(stmts: &[Statement], out: &mut HashSet<String>) {
    for stmt in stmts {
        match stmt {
            Statement::VarDecl {
                var_type: VarType::Real,
                name,
                ..
            } => {
                out.insert(name.clone());
            }
            Statement::If {
                then_body,
                else_body,
                ..
            } => {
                collect_real_locals(then_body, out);
                collect_real_locals(else_body, out);
            }
            Statement::While { body, .. }
            | Statement::DoWhile { body, .. }
            | Statement::For { body, .. }
            | Statement::ForC { body, .. }
            | Statement::Block { body } => collect_real_locals(body, out),
            Statement::Switch { cases, default, .. } => {
                for (_, body) in cases {
                    collect_real_locals(body, out);
                }
                collect_real_locals(default, out);
            }
            _ => {}
        }
    }
}

fn collect_assigns(stmts: &[Statement], out: &mut Vec<(String, Expr)>) {
    for stmt in stmts {
        match stmt {
            Statement::Assign {
                target: Expr::Var(name),
                value,
                ..
            } => out.push((name.clone(), value.clone())),
            Statement::If {
                then_body,
                else_body,
                ..
            } => {
                collect_assigns(then_body, out);
                collect_assigns(else_body, out);
            }
            Statement::While { body, .. }
            | Statement::DoWhile { body, .. }
            | Statement::For { body, .. }
            | Statement::Block { body } => collect_assigns(body, out),
            Statement::ForC {
                init, update, body, ..
            } => {
                collect_assigns(std::slice::from_ref(init), out);
                collect_assigns(std::slice::from_ref(update), out);
                collect_assigns(body, out);
            }
            Statement::Switch { cases, default, .. } => {
                for (_, body) in cases {
                    collect_assigns(body, out);
                }
                collect_assigns(default, out);
            }
            _ => {}
        }
    }
}

/// Variables this condition tests against zero, in any direction, including
/// through the `TA_IS_ZERO*` helpers.
fn zero_tested(cond: &Expr, out: &mut HashSet<String>) {
    match cond {
        Expr::BinOp(lhs, BinOp::And | BinOp::Or, rhs) => {
            zero_tested(lhs, out);
            zero_tested(rhs, out);
        }
        Expr::Not(inner) => zero_tested(inner, out),
        Expr::BinOp(lhs, op, rhs) if is_comparison(op) => {
            if is_zero(rhs) {
                if let Expr::Var(v) = lhs.as_ref() {
                    out.insert(v.clone());
                }
            }
            if is_zero(lhs) {
                if let Expr::Var(v) = rhs.as_ref() {
                    out.insert(v.clone());
                }
            }
        }
        // `TA_IS_ZERO(s)` / `TA_IS_ZERO_SCALED(s, ref)`: the whole call is a
        // test of its first argument against zero.
        Expr::FuncCall(name, args) if name.contains("IS_ZERO") => {
            if let Some(Expr::Var(v)) = args.first() {
                out.insert(v.clone());
            }
        }
        _ => {}
    }
}

fn is_comparison(op: &BinOp) -> bool {
    matches!(
        op,
        BinOp::Less | BinOp::LessEq | BinOp::Greater | BinOp::GreaterEq | BinOp::Eq | BinOp::NotEq
    )
}

fn is_zero(e: &Expr) -> bool {
    match e {
        Expr::Literal(v) => *v == 0.0,
        Expr::IntLiteral(v) => *v == 0,
        _ => false,
    }
}

/// A condition, spelled just far enough to recognise it in the source.
fn describe(cond: &Expr) -> String {
    match cond {
        Expr::Var(v) => v.clone(),
        Expr::Literal(v) => format!("{v}"),
        Expr::IntLiteral(v) => format!("{v}"),
        Expr::Not(inner) => format!("!{}", describe(inner)),
        Expr::FuncCall(name, args) => {
            let inner: Vec<String> = args.iter().map(describe).collect();
            format!("{name}({})", inner.join(", "))
        }
        Expr::ArrayAccess(name, idx) => format!("{name}[{}]", describe(idx)),
        Expr::BinOp(lhs, op, rhs) => {
            format!("{} {} {}", describe(lhs), op_str(op), describe(rhs))
        }
        _ => "...".to_string(),
    }
}

fn op_str(op: &BinOp) -> &'static str {
    match op {
        BinOp::Add => "+",
        BinOp::Sub => "-",
        BinOp::Mul => "*",
        BinOp::Div => "/",
        BinOp::Mod => "%",
        BinOp::LessEq => "<=",
        BinOp::Less => "<",
        BinOp::Greater => ">",
        BinOp::GreaterEq => ">=",
        BinOp::Eq => "==",
        BinOp::NotEq => "!=",
        BinOp::And => "&&",
        BinOp::Or => "||",
        BinOp::Shr => ">>",
        BinOp::Shl => "<<",
        BinOp::BitwiseOr => "|",
        BinOp::BitwiseXor => "^",
        BinOp::BitwiseAnd => "&",
    }
}
