use crate::scripting::{component::Component, system::{Context, query::Query}};

pub trait SystemParam {
    type Item<'a>;
    fn fetch<'a>(ctx: &'a Context) -> Self::Item<'a>;
}

pub trait IntoSystem<Marker> {
    fn run(&self, ctx: &Context);
}

impl<T: Component> SystemParam for Query<'_, T> {
    type Item<'a> = Query<'a, T>;

    fn fetch<'a>(ctx: &'a Context) -> Self::Item<'a> {
        Query::new(ctx)
    }
}

impl SystemParam for &Context {
    type Item<'a> = &'a Context;

    fn fetch<'a>(ctx: &'a Context) -> Self::Item<'a> {
        ctx
    }
}

macro_rules! impl_into_system {
    ($($p:ident),+) => {
        impl<Func, $($p: SystemParam),+> IntoSystem<($($p,)+)> for Func
        where Func: Fn($($p),+) + for<'a> Fn($($p::Item<'a>),+) {
            fn run(&self, ctx: &Context) {
                self($($p::fetch(ctx)),+)
            }
        }
    };
}

macro_rules! system_param {
    ($($(#[$meta:meta])* $name:ident($ty:ty) $fetch:expr)+) => {
        $(
            $(#[$meta])*
            #[derive(Clone, Copy, Debug)]
            pub struct $name(pub $ty);
        )+

        $(impl SystemParam for $name {
            type Item<'a> = $name;
            fn fetch<'a>(ctx: &'a Context) -> Self::Item<'a> {
                $name($fetch(ctx))
            }
        })+

        $(impl std::ops::Deref for $name {
            type Target = $ty;
            fn deref(&self) -> &Self::Target {
                &self.0
            }
        })+
    };
}

system_param!(
    EventTarget(Option<crate::prelude::ObjectID>) |ctx: &Context| ctx.event_target()
    Time(f64) |ctx: &Context| ctx.time()
    DeltaTime(f32) |ctx: &Context| ctx.delta_time()
);

impl_into_system!(A);
impl_into_system!(A, B);
impl_into_system!(A, B, C);
impl_into_system!(A, B, C, D);
impl_into_system!(A, B, C, D, E);
impl_into_system!(A, B, C, D, E, F);
impl_into_system!(A, B, C, D, E, F, G);
impl_into_system!(A, B, C, D, E, F, G, H);
impl_into_system!(A, B, C, D, E, F, G, H, I);
impl_into_system!(A, B, C, D, E, F, G, H, I, J);
impl_into_system!(A, B, C, D, E, F, G, H, I, J, K);
impl_into_system!(A, B, C, D, E, F, G, H, I, J, K, L);