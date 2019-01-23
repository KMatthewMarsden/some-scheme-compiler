use std::borrow::Cow;

use crate::nodes::{LExpr, ExprLit, LamType};

// compiler transformation stage

#[derive(Default)]
pub struct TransformContext {
    genvar_count: u64,
}

impl TransformContext {
    pub fn gen_ident<'a>(&mut self, name: &str) -> Cow<'a, str> {
        let var = format!("$anon_var_{}_{}", name, self.genvar_count);
        self.genvar_count += 1;
        Cow::from(var)
    }

    pub fn gen_var<'a>(&mut self, name: &str) -> LExpr<'a> {
        LExpr::Var(self.gen_ident(name))
    }

    pub fn gen_cont<'a>(&mut self) -> LExpr<'a> {
        let var = format!("$cont_var_{}", self.genvar_count);
        self.genvar_count += 1;
        LExpr::Var(Cow::from(var))
    }

    pub fn gen_throwaway<'a>(&mut self) -> Cow<'a, str> {
        let var = format!("$throwaway_var_{}", self.genvar_count);
        self.genvar_count += 1;
        Cow::from(var)
    }

    pub fn gen_throwaway_var<'a>(&mut self) -> LExpr<'a> {
        LExpr::Var(self.gen_throwaway())
    }
}

fn void_obj() -> LExpr<'static> {
    LExpr::Lit(ExprLit::Void)
}


/// Renames some functions to their builtin equivalent
///
/// For example:
/// ```scheme
/// (+ 1 2)
/// ```
///
/// becomes
///
/// ```scheme
/// (builtin_int_obj_add_func_1 1 2)
/// ```
pub fn rename_builtins<'a>(expr: LExpr<'a>, ctx: &mut TransformContext) -> LExpr<'a> {
    use crate::nodes::LExpr::*;

    match expr {
        Lam(args, body) => {
            let body: Vec<_> = body
                .into_iter()
                .map(|e| rename_builtins(e, ctx))
                .collect();
            Lam(args, body)
        },
        App(box operator, operands) => {
            let operator = rename_builtins(operator, ctx);
            let operands: Vec<_> = operands
                .into_iter()
                .map(|e| rename_builtins(e, ctx))
                .collect();
            App(box operator, operands)
        }
        Var(var) => {
            let builtin_name = match var.as_ref() {
                "to_string" => "to_string_func",
                "println" => "println_func",
                "+" => "object_int_obj_add",
                "-" => "object_int_obj_sub",
                "*" => "object_int_obj_mul",
                "/" => "object_int_obj_div",
                _   => return Var(var),
            };
            BuiltinIdent(Cow::from(builtin_name), LamType::TwoArg)
        },
        Lit(..) | BuiltinIdent(..) => expr,
        _ => unreachable!("Shouldn't be touching this yet."),
    }
}


/// Transforms literals into their correct literal constructor form
///
/// For example:
/// ```scheme
/// 12
/// ```
///
/// becomes
///
/// ```scheme
/// (object_int_obj_new 12)
/// ```
pub fn transform_lits<'a>(expr: LExpr<'a>, ctx: &mut TransformContext) -> LExpr<'a> {
    use crate::nodes::LExpr::*;

    match expr {
        Lam(args, body) => {
            let body: Vec<_> = body
                .into_iter()
                .map(|e| transform_lits(e, ctx))
                .collect();
            Lam(args, body)
        },
        App(box operator, operands) => {
            let operator = transform_lits(operator, ctx);
            let operands: Vec<_> = operands
                .into_iter()
                .map(|e| transform_lits(e, ctx))
                .collect();
            App(box operator, operands)
        }
        Var(..) | BuiltinIdent(..) | Lit(..) => expr,
        _ => unreachable!("Shouldn't be touching this yet."),
    }
}


/// Transform multiple parameter lambdas into nested single parmeters.
///
/// ```scheme
/// (lambda (a b c) ...)
/// becomes
/// (lambda (a)
///   (lambda (b)
///     (lambda (c)
///       ...)))
/// ```
///
/// Transform calls with multiple parameters into nested calls each applying single parameters
///
/// ```scheme
/// (f a b c)
/// ```
///
/// becomes
///
/// ```scheme
/// ((((f) a) b) c)
/// ```
pub fn expand_lam_app<'a>(expr: LExpr<'a>, ctx: &mut TransformContext) -> LExpr<'a> {
    use crate::nodes::LExpr::*;

    match expr {
        Lam(args, body) => {
            let body: Vec<_> = body
                .into_iter()
                .map(|x| expand_lam_app(x, ctx))
                .collect();
            match args.len() {
                0 => LamOne(ctx.gen_throwaway(), body),
                _ => {
                    let mut iter = args.into_iter().rev();

                    let first = LamOne(iter.next().unwrap(), body);

                    iter.fold(first, |acc, arg| LamOne(arg, vec![acc]))
                }
            }
        }
        App(box operator, operands) => {
            let operator = expand_lam_app(operator, ctx);
            let operands: Vec<_> = operands
                .into_iter()
                .map(|o| expand_lam_app(o, ctx))
                .collect();
            let num_operands = operands.len();
            match num_operands {
                0 => AppOne(box operator, box void_obj()),
                _ => {
                    let mut operands = operands.into_iter();

                    let first = AppOne(box operator, box operands.next().unwrap());

                    operands.fold(first, |acc, arg| AppOne(box acc, box arg))
                }
            }
        }
        Var(..) | Lit(..) | BuiltinIdent(..) => expr,
        _ => unreachable!("Shouldn't be touching this yet"),
    }
}

/// Transform body of lambda from multiple expressions into a single expression
///
/// (lambda () a b c)
///
/// becomes:
///
/// (lambda ()
///  ((lambda ($unique) c)
///   ((lambda ($unique) b)
///    a)))
///
/// where $unique is a unique variable name
pub fn expand_lam_body<'a>(expr: LExpr<'a>, ctx: &mut TransformContext) -> LExpr<'a> {
    use crate::nodes::LExpr::*;

    match expr {
        LamOne(arg, body) => {
            let num_body = body.len();
            let body: Vec<_> = body
                .into_iter()
                .rev()
                .map(|b| expand_lam_body(b, ctx))
                .collect();
            let inner = match num_body {
                0 => LamOneOne(arg.clone(), box void_obj()),
                _ => {
                    // get the last expression, as this won't be placed in a (... x) wrapper
                    let mut body = body.into_iter();
                    let first = body.next().unwrap();

                    body.fold(first, |acc, arg| {
                        AppOne(box LamOneOne(ctx.gen_ident("lam_expand"), box acc), box arg)
                    })
                }
            };
            LamOneOne(arg.clone(), box inner)
        }
        AppOne(box operator, box operand) => AppOne(
            box expand_lam_body(operator, ctx),
            box expand_lam_body(operand, ctx),
        ),
        x => x,
    }
}

/// Apply the cps transformation with a continuation expression
pub fn cps_transform_cont<'a>(
    expr: LExpr<'a>,
    cont: LExpr<'a>,
    ctx: &mut TransformContext,
) -> LExpr<'a> {
    match expr {
        LExpr::Var(..) |
        LExpr::LamOneOne(..) |
        LExpr::LamOneOneCont(..) |
        LExpr::BuiltinIdent(..) |
        LExpr::Lit(..) =>
            LExpr::AppOne(box cont, box cps_transform(expr, ctx)),
        LExpr::AppOne(box operator, box operand) => {
            let rator_var: Cow<'a, str> = ctx.gen_ident("rator_var");
            let rator_var_expr = LExpr::Var(rator_var.clone());

            let rand_var: Cow<'a, str> = ctx.gen_ident("rand_var");
            let rand_var_expr = LExpr::Var(rand_var.clone());

            cps_transform_cont(
                operator,
                LExpr::LamOneOne(
                    rator_var,
                    box cps_transform_cont(
                        operand,
                        LExpr::LamOneOne(
                            rand_var,
                            box LExpr::AppOneCont(
                                box rator_var_expr,
                                box rand_var_expr,
                                box cont
                            )
                        ), ctx)
                ), ctx)
        }
        LExpr::AppOneCont(..) => unreachable!("This shouldn't be visited"),
        LExpr::Lam(..) | LExpr::App(..) | LExpr::LamOne(..) => unreachable!("These shouldn't exist here"),
    }
}

/// Apply the cps transformation
pub fn cps_transform<'a>(expr: LExpr<'a>, ctx: &mut TransformContext) -> LExpr<'a> {
    match expr {
        LExpr::LamOneOne(arg, box expr) => {

            let cont_var: Cow<'a, str> = ctx.gen_ident("cont_var");
            let cont_var_exp = LExpr::Var(cont_var.clone());

            LExpr::LamOneOneCont(
                arg,
                cont_var.clone(),
                box cps_transform_cont(expr, cont_var_exp, ctx),
            )
        }
        LExpr::LamOneOneCont(..) => panic!("Are we supposed to see this here?"),
        x => x
    }
}
