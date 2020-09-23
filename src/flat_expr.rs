use moniker::BoundTerm;
use moniker::{Binder, Ignore, Scope, Var};

use pretty::{BoxAllocator, DocAllocator, DocBuilder};
use termcolor::{Color, ColorSpec, WriteColor};

use std::collections::HashMap;
use std::{io::Result, rc::Rc};

use crate::lifted_expr::{LExpr, LiftedLambda};
use crate::literals::Literal;
use crate::utils::clone_rc;

#[derive(Debug, Clone, BoundTerm)]
pub enum FExpr {
    LamOne(Scope<Binder<String>, Rc<FExpr>>),
    LamTwo(Scope<Binder<String>, Scope<Binder<String>, Rc<FExpr>>>),
    Var(Var<String>),
    Lit(Ignore<Literal>),
    BuiltinIdent(Ignore<String>),
    CallOne(Rc<FExpr>, Rc<FExpr>),
    CallTwo(Rc<FExpr>, Rc<FExpr>, Rc<FExpr>),
}

struct LiftingCtx {
    id_counter: usize,
    lambdas: HashMap<usize, LiftedLambda>,
}

impl LiftingCtx {
    fn new() -> Self {
        Self {
            id_counter: 0,
            lambdas: HashMap::new(),
        }
    }
    fn get(&mut self) -> usize {
        let next = self.id_counter + 1;
        std::mem::replace(&mut self.id_counter, next)
    }
    fn add(&mut self, l: LiftedLambda) {
        self.lambdas.insert(l.id, l);
    }
}

impl FExpr {
    pub fn pretty<'a, D>(&self, allocator: &'a D) -> DocBuilder<'a, D, ColorSpec>
    where
        D: DocAllocator<'a, ColorSpec>,
        D::Doc: Clone,
    {
        match self {
            FExpr::LamOne(s) => {
                let Scope {
                    unsafe_pattern: pat,
                    unsafe_body: body,
                } = &s;

                let pat_pret = allocator
                    .as_string(pat)
                    .annotate(ColorSpec::new().set_fg(Some(Color::Green)).clone())
                    .parens();
                let body_pret = allocator
                    .line_()
                    .append(body.pretty(allocator))
                    .nest(1)
                    .group();

                allocator
                    .text("lambda")
                    .annotate(ColorSpec::new().set_fg(Some(Color::Magenta)).clone())
                    .append(allocator.space())
                    .append(pat_pret)
                    .append(allocator.space())
                    .append(body_pret)
                    .parens()
            }
            FExpr::LamTwo(s) => {
                let Scope {
                    unsafe_pattern: pat,
                    unsafe_body:
                        Scope {
                            unsafe_pattern: cont,
                            unsafe_body: body,
                        },
                } = &s;

                let pat_pret = allocator
                    .as_string(pat)
                    .annotate(ColorSpec::new().set_fg(Some(Color::Green)).clone());
                let cont_pret = allocator
                    .as_string(cont)
                    .annotate(ColorSpec::new().set_fg(Some(Color::Red)).clone());
                let args_pret = pat_pret
                    .append(allocator.space())
                    .append(cont_pret)
                    .parens();
                let body_pret = allocator
                    .line_()
                    .append(body.pretty(allocator))
                    .nest(1)
                    .group();

                allocator
                    .text("lambda")
                    .annotate(ColorSpec::new().set_fg(Some(Color::Magenta)).clone())
                    .append(allocator.space())
                    .append(args_pret)
                    .append(allocator.space())
                    .append(body_pret)
                    .parens()
            }
            FExpr::Var(s) => allocator.as_string(s),
            FExpr::Lit(Ignore(l)) => l.pretty(allocator),
            FExpr::BuiltinIdent(Ignore(s)) => allocator.as_string(s),
            FExpr::CallOne(f, c) => {
                let f_pret = f.pretty(allocator);
                let c_pret = c.pretty(allocator);

                f_pret
                    .annotate(ColorSpec::new().set_fg(Some(Color::Blue)).clone())
                    .append(allocator.space())
                    .append(c_pret)
                    .parens()
            }
            FExpr::CallTwo(f, v, c) => {
                let f_pret = f.pretty(allocator);
                let v_pret = v.pretty(allocator);
                let c_pret = c.pretty(allocator);

                f_pret
                    .annotate(ColorSpec::new().set_fg(Some(Color::Blue)).clone())
                    .append(allocator.space())
                    .append(v_pret)
                    .append(allocator.space())
                    .append(c_pret)
                    .parens()
            }
        }
    }

    pub fn pretty_print(&self, out: impl WriteColor) -> Result<()> {
        let allocator = BoxAllocator;

        self.pretty(&allocator).1.render_colored(70, out)?;

        Ok(())
    }

    pub fn lift_lambdas(self) -> (LExpr, HashMap<usize, LiftedLambda>) {
        let mut ctx = LiftingCtx::new();
        let expr = self.lift_lambdas_internal(&mut ctx);
        (expr, ctx.lambdas)
    }

    fn lift_lambdas_internal(self, ctx: &mut LiftingCtx) -> LExpr {
        match self {
            FExpr::LamOne(s) => {
                let (param, body) = s.unbind();
                let body = clone_rc(body).lift_lambdas_internal(ctx);
                let id = ctx.get();
                ctx.add(LiftedLambda::new(
                    id,
                    vec![param.0],
                    body.free_vars(),
                    Rc::new(body),
                ));
                LExpr::Lifted(Ignore(id))
            }
            FExpr::LamTwo(s) => {
                let (param0, body) = s.unbind();
                let (param1, body) = body.unbind();
                let body = clone_rc(body).lift_lambdas_internal(ctx);
                let id = ctx.get();
                ctx.add(LiftedLambda::new(
                    id,
                    vec![param0.0, param1.0],
                    body.free_vars(),
                    Rc::new(body),
                ));
                LExpr::Lifted(Ignore(id))

            }
            FExpr::Var(v) => LExpr::Var(v),
            FExpr::Lit(l) => LExpr::Lit(l),
            FExpr::BuiltinIdent(i) => LExpr::BuiltinIdent(i),
            FExpr::CallOne(f, p) => {
                let f = clone_rc(f).lift_lambdas_internal(ctx);
                let p = clone_rc(p).lift_lambdas_internal(ctx);
                LExpr::CallOne(Rc::new(f), Rc::new(p))
            }
            FExpr::CallTwo(f, p, k) => {
                let f = clone_rc(f).lift_lambdas_internal(ctx);
                let p = clone_rc(p).lift_lambdas_internal(ctx);
                let k = clone_rc(k).lift_lambdas_internal(ctx);
                LExpr::CallTwo(Rc::new(f), Rc::new(p), Rc::new(k))
            }
        }
    }
}
