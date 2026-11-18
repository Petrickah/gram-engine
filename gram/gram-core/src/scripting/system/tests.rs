//! Query, the system parameters and IntoSystem, against a fake host: `get_component` looks the
//! component up in a per-thread table the test fills, standing in for C++'s World.

use std::cell::RefCell;
use std::ffi::{c_char, c_void};
use std::panic::{AssertUnwindSafe, catch_unwind};

use crate::interop::host::{HostAPI, install_test_host};
use crate::interop::logging::LogLevel;
use crate::scripting::component::{Component, ComponentID, component_id};
use crate::scripting::system::{Context, DeltaTime, Descriptor, EventTarget, IntoSystem, Query, Time};
use crate::scripting::world::{INVALID, ObjectID};

#[repr(C)]
#[derive(Debug, PartialEq)]
struct Health { value: i32 }
impl Component for Health { const ID: ComponentID = component_id("Health"); }

#[repr(C)]
#[derive(Debug, PartialEq)]
struct Speed { value: f32 }
impl Component for Speed { const ID: ComponentID = component_id("Speed"); }

const A: ObjectID = ObjectID::new(0, 0);
const B: ObjectID = ObjectID::new(1, 0);
const DEAD: ObjectID = ObjectID::new(0, 1);   // A's slot, a later generation

thread_local! {
    // (entity, component, address). Each test runs on its own thread, so starts empty; the
    // components are leaked: they must outlive every reference the test takes.
    static WORLD: RefCell<Vec<(ObjectID, ComponentID, *mut c_void)>> = const { RefCell::new(Vec::new()) };
}

extern "C" fn log(_: LogLevel, _: *const c_char) {}
extern "C" fn register_system(_: *mut c_void, _: *const Descriptor) {}
extern "C" fn get_component(_: *mut c_void, entity: ObjectID, component: u64) -> *mut c_void {
    WORLD.with(|world| {
        world.borrow().iter()
            .find(|(e, c, _)| *e == entity && *c == component)
            .map_or(std::ptr::null_mut(), |&(_, _, ptr)| ptr)
    })
}

// In insertion order, like C++'s storage order: the n-th entry with that component is position n.
extern "C" fn get_component_count(_: *mut c_void, component: u64) -> u32 {
    WORLD.with(|world| world.borrow().iter().filter(|(_, c, _)| *c == component).count() as u32)
}
extern "C" fn get_component_at(_: *mut c_void, component: u64, position: u32, entity: *mut ObjectID) -> *mut c_void {
    WORLD.with(|world| {
        match world.borrow().iter().filter(|(_, c, _)| *c == component).nth(position as usize) {
            Some(&(owner, _, ptr)) => {
                if let Some(entity) = unsafe { entity.as_mut() } { *entity = owner; }
                ptr
            }
            None => std::ptr::null_mut(),
        }
    })
}

fn setup() {
    install_test_host(HostAPI {
        host: std::ptr::null_mut(), log, register_system, get_component, get_component_count, get_component_at,
    });
}

fn add<T: Component>(entity: ObjectID, value: T) {
    let ptr = Box::into_raw(Box::new(value)) as *mut c_void;
    WORLD.with(|world| world.borrow_mut().push((entity, T::ID, ptr)));
}

fn ctx(target: ObjectID) -> Context { Context::new(0.25, 10.5, target) }

fn panics(f: impl FnOnce()) -> bool { catch_unwind(AssertUnwindSafe(f)).is_err() }

// --- Query ------------------------------------------------------------------------------------

#[test]
fn get_reads_the_component() {
    setup();
    add(A, Health { value: 3 });
    let ctx = ctx(INVALID);
    let health = Query::<Health>::new(&ctx);
    assert_eq!(health.get(A).map(|h| h.value), Some(3));
}

#[test]
fn get_mut_writes_through_to_the_host() {
    setup();
    add(A, Health { value: 3 });
    let ctx = ctx(INVALID);
    let health = Query::<Health>::new(&ctx);
    if let Some(mut h) = health.get_mut(A) { h.value += 1; }
    assert_eq!(health.get(A).map(|h| h.value), Some(4));
}

#[test]
fn a_missing_component_or_a_dead_entity_is_none() {
    setup();
    add(A, Health { value: 3 });
    let ctx = ctx(INVALID);
    assert!(Query::<Speed>::new(&ctx).get(A).is_none(), "A has no Speed");
    assert!(Query::<Health>::new(&ctx).get(B).is_none(), "B has nothing");
    assert!(Query::<Health>::new(&ctx).get_mut(DEAD).is_none(), "stale generation");
}

#[test]
fn a_none_leaves_no_borrow_behind() {
    setup();
    let ctx = ctx(INVALID);
    let health = Query::<Health>::new(&ctx);
    assert!(health.get_mut(A).is_none());
    add(A, Health { value: 3 });
    let _write = health.get_mut(A).expect("the failed get_mut released its borrow");
}

#[test]
fn two_reads_can_overlap() {
    setup();
    add(A, Health { value: 3 });
    let ctx = ctx(INVALID);
    let health = Query::<Health>::new(&ctx);
    let (first, second) = (health.get(A), health.get(A));
    assert!(first.is_some() && second.is_some());
}

#[test]
fn a_write_while_a_read_is_alive_panics() {
    setup();
    add(A, Health { value: 3 });
    let ctx = ctx(INVALID);
    let health = Query::<Health>::new(&ctx);
    let _read = health.get(A);
    assert!(panics(|| { health.get_mut(A); }));
}

#[test]
fn a_second_query_on_the_same_component_conflicts_too() {
    // The borrows are per entity + component, not per Query.
    setup();
    add(A, Health { value: 3 });
    let ctx = ctx(INVALID);
    let _write = Query::<Health>::new(&ctx).get_mut(A);
    assert!(panics(|| { Query::<Health>::new(&ctx).get(A); }));
}

#[test]
fn writes_to_different_entities_or_components_dont_conflict() {
    setup();
    add(A, Health { value: 3 });
    add(B, Health { value: 5 });
    add(A, Speed { value: 1.5 });
    let ctx = ctx(INVALID);
    let (health, speed) = (Query::<Health>::new(&ctx), Query::<Speed>::new(&ctx));

    let (mut a, mut b) = (health.get_mut(A).unwrap(), health.get_mut(B).unwrap());
    std::mem::swap(&mut a.value, &mut b.value);
    let _speed = speed.get_mut(A).unwrap();
    drop((a, b));

    assert_eq!(health.get(A).map(|h| h.value), Some(5));
    assert_eq!(health.get(B).map(|h| h.value), Some(3));
}

#[test]
fn a_dropped_guard_releases_its_borrow() {
    setup();
    add(A, Health { value: 3 });
    let ctx = ctx(INVALID);
    let health = Query::<Health>::new(&ctx);
    { let _read = health.get(A); }
    { let _write = health.get_mut(A); }
    let _again = health.get_mut(A).expect("both earlier borrows are gone");
}

// --- System parameters ------------------------------------------------------------------------

#[test]
fn event_target_is_none_for_the_invalid_id() {
    let seen = RefCell::new(Vec::new());   // a system is Fn: it can't mutate what it captures
    for target in [INVALID, A] {
        let run = |target: EventTarget| seen.borrow_mut().push(*target);
        IntoSystem::run(&run, &ctx(target));
    }
    assert_eq!(seen.into_inner(), [None, Some(A)]);
}

#[test]
fn time_and_delta_time_come_from_the_context() {
    let seen = RefCell::new(None);
    let run = |time: Time, delta: DeltaTime| *seen.borrow_mut() = Some((*time, *delta));
    IntoSystem::run(&run, &ctx(INVALID));
    assert_eq!(seen.into_inner(), Some((10.5, 0.25)));
}

// --- IntoSystem -------------------------------------------------------------------------------

fn plain(ctx: &Context) { assert_eq!(ctx.time(), 10.5); }

fn heal(target: EventTarget, health: Query<Health>) {
    let Some(entity) = *target else { return };
    if let Some(mut h) = health.get_mut(entity) { h.value += 10; }
}

#[test]
fn a_plain_context_function_is_still_a_system() {
    IntoSystem::run(&plain, &ctx(INVALID));
}

#[test]
fn a_function_of_several_params_runs_with_them_fetched() {
    setup();
    add(A, Health { value: 3 });
    // What export_game! generates: a non-capturing closure, coerced to the fn(&Context) C++ calls.
    let system: fn(&Context) = |ctx| IntoSystem::run(&heal, ctx);

    system(&ctx(A));
    system(&ctx(INVALID));   // no target: nothing to heal

    let ctx = ctx(INVALID);
    assert_eq!(Query::<Health>::new(&ctx).get(A).map(|h| h.value), Some(13));
}

#[test]
fn a_system_that_panics_releases_its_borrows() {
    // What `guard` sees: the panic unwinds through the guard, whose Drop runs.
    setup();
    add(A, Health { value: 3 });
    fn fails(target: EventTarget, health: Query<Health>) {
        let _write = health.get_mut(target.unwrap());
        panic!("the system failed");
    }
    assert!(panics(|| IntoSystem::run(&fails, &ctx(A))));
    let ctx = ctx(INVALID);
    let _next_frame = Query::<Health>::new(&ctx).get_mut(A).expect("released by the unwind");
}

// --- The host's iteration (what Query::iter will walk) ------------------------------------------

#[test]
fn the_host_walks_one_component_type_by_position() {
    setup();
    add(A, Health { value: 10 });
    add(B, Speed { value: 2.0 });
    add(B, Health { value: 20 });

    let host = crate::interop::host::host().unwrap();
    assert_eq!((host.get_component_count)(host.host, Health::ID), 2);
    assert_eq!((host.get_component_count)(host.host, component_id("Missing")), 0);

    let mut owner = INVALID;
    let second = (host.get_component_at)(host.host, Health::ID, 1, &mut owner) as *const Health;
    assert_eq!(owner, B);
    assert_eq!(unsafe { &*second }, &Health { value: 20 });
    assert!((host.get_component_at)(host.host, Health::ID, 2, &mut owner).is_null());
}
