use itertools::Itertools;
use std::{
    boxed::Box,
    borrow::Cow,
    fmt,
    collections::HashMap,
};

type Cont<'a> = Box<LExpr<'a>>;

#[derive(Debug, Clone)]
pub enum LExpr<'a> {
    Lam(Vec<Cow<'a, str>>, Vec<LExpr<'a>>),
    App(Box<LExpr<'a>>, Vec<LExpr<'a>>),
    Var(Cow<'a, str>),

    LamOne(Cow<'a, str>, Vec<LExpr<'a>>),

    AppOne(Box<LExpr<'a>>, Box<LExpr<'a>>),

    LamOneOne(Cow<'a, str>, Box<LExpr<'a>>),

    AppOneCont(Box<LExpr<'a>>, Box<LExpr<'a>>, Cont<'a>),
    LamOneOneCont(Cow<'a, str>, Cow<'a, str>, Box<LExpr<'a>>),
}


#[derive(Debug, Clone)]
pub struct Env<'a>(pub HashMap<Cow<'a, str>, usize>);


impl<'a> Env<'a> {
    pub fn new(parent: &Env<'a>, vals: impl IntoIterator<Item=(Cow<'a, str>, usize)>) -> Self {
        let mut new_map = HashMap::new();
        for (k, v) in parent.0.iter() {
            new_map.insert(k.clone(), v.clone());
        }
        // new_map.extend(parent.0);
        new_map.extend(vals);
        Env (new_map)
    }

    pub fn empty() -> Self {
        Env (HashMap::new())
    }

    pub fn get(&self, key: &Cow<'a, str>) -> Option<usize> {
        self.0.get(key).map(|&a| a)
    }
}


/// Expressions that have an explicit environment.
#[derive(Debug, Clone)]
pub enum LExEnv<'a> {
    Lam { arg: Cow<'a, str>,
          expr: Box<LExEnv<'a>>,
          env: Env<'a>,
          id: usize,
    },
    LamCont { arg: Cow<'a, str>,
              cont: Cow<'a, str>,
              expr: Box<LExEnv<'a>>,
              env: Env<'a>,
              id: usize,
    },
    App1 { cont: Box<LExEnv<'a>>,
           rand: Box<LExEnv<'a>>,
           env: Env<'a>,
    },
    App2 { rator: Box<LExEnv<'a>>,
           rand: Box<LExEnv<'a>>,
           cont: Box<LExEnv<'a>>,
           env: Env<'a>,
    },
    Var { name: Cow<'a, str>,
          global: bool,
          env: Env<'a>,
    },
    LamRef {
        id: usize,
    }
}


impl<'a> fmt::Display for LExpr<'a> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use nodes::LExpr::*;

        match self {
            Lam(args, body) => {
                write!(f, "(lambda ({})", args.iter().join(" "))?;
                for expr in body {
                    write!(f, " {}", expr)?;
                }
                write!(f, ")")
            },
            App(box operator, operands) => {
                write!(f, "({}", operator)?;
                for operand in operands {
                    write!(f, " {}", operand)?;
                }
                write!(f, ")")
            },
            Var(name) =>
                write!(f, "{}", name),
            LamOneOne(arg, box expr) =>
                write!(f, "(lambda ({}) {})", arg, expr),
            AppOne(box operator, box operands) =>
                write!(f, "({} {})", operator, operands),
            LamOne(arg, body) => {
                write!(f, "(lambda ({})", arg)?;
                for expr in body {
                    write!(f, " {}", expr)?;
                }
                write!(f, ")")
            },
            LamOneOneCont(arg, cont, box expr) =>
                write!(f, "(lambda ({} {}) {})", arg, cont, expr),
            AppOneCont(box operator, box operand, box cont) =>
                write!(f, "({} {} {})", operator, operand, cont),
        }
    }
}


impl<'a> fmt::Display for LExEnv<'a> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use nodes::LExEnv::*;

        match self {
            Lam {arg, expr, ..} =>
                write!(f, "(lambda ({}) {})", arg, expr),
            LamCont {arg, cont, expr, ..} =>
                write!(f, "(lambda ({} {}) {})", arg, cont, expr),
            App1 {cont, rand, ..} =>
                write!(f, "({} {})", cont, rand),
            App2 {rator, rand, cont, ..} =>
                write!(f, "({} {} {})", rator, rand, cont),
            Var {name, ..} =>
                write!(f, "{}", name),
            LamRef {id} =>
                write!(f, "lambda:{}", id),
        }
    }
}
