use std::cell::RefCell;

use crate::scripting::component::ComponentID;
use crate::scripting::world::ObjectID;

struct Borrow { entity: ObjectID, component: ComponentID, mutable: bool }

pub struct BorrowGuard { entity: ObjectID, component: ComponentID, mutable: bool }

thread_local! {
    static BORROWS: RefCell<Vec<Borrow>> = RefCell::new(Vec::new());
}

impl BorrowGuard {
    #[track_caller]
    pub fn acquire(entity: ObjectID, component: ComponentID, mutable: bool) -> BorrowGuard {
        let conflict = BORROWS.with(|list| {
            let mut list = list.borrow_mut();
            let conflict = list.iter().any(|b| b.entity == entity && b.component == component && (b.mutable || mutable));
            if !conflict { list.push(Borrow { entity, component, mutable }); }
            conflict
        });

        if conflict { panic!("component {component:#x} of {entity:?} is already borrowed{}", if mutable { "" } else { " mutably" }); }

        BorrowGuard { entity, component, mutable }
    }
}

impl Drop for BorrowGuard {
    fn drop(&mut self) {
        BORROWS.with(|list| {
            let mut list = list.borrow_mut();
            let Some(position) = list.iter().position(|b| b.entity == self.entity && b.component == self.component && (b.mutable == self.mutable)) else { return };
            list.remove(position);
        });
    }
}


#[cfg(test)]
mod tests {
    use super::*;
    use std::panic::{AssertUnwindSafe, catch_unwind};

    const A: ObjectID = ObjectID::new(0, 0);
    const B: ObjectID = ObjectID::new(1, 0);
    const GREETING: ComponentID = 1;
    const HEALTH: ComponentID = 2;

    /// The panic message of `f`, or `None` if it didn't panic. (Each test runs on its own thread,
    /// so each starts with an empty BORROWS.)
    fn panic_of(f: impl FnOnce()) -> Option<String> {
        let payload = catch_unwind(AssertUnwindSafe(f)).err()?;
        Some(payload.downcast_ref::<String>().cloned().unwrap_or_default())
    }

    fn active() -> usize {
        BORROWS.with(|list| list.borrow().len())
    }

    #[test]
    fn two_reads_can_overlap() {
        let _a = BorrowGuard::acquire(A, GREETING, false);
        let _b = BorrowGuard::acquire(A, GREETING, false);
        assert_eq!(active(), 2);
    }

    #[test]
    fn a_write_inside_a_write_panics() {
        let _outer = BorrowGuard::acquire(A, GREETING, true);
        let message = panic_of(|| { BorrowGuard::acquire(A, GREETING, true); }).expect("should panic");
        assert!(message.ends_with("is already borrowed"), "{message}");
    }

    #[test]
    fn a_write_inside_a_read_panics() {
        let _outer = BorrowGuard::acquire(A, GREETING, false);
        assert!(panic_of(|| { BorrowGuard::acquire(A, GREETING, true); }).is_some());
    }

    #[test]
    fn a_read_inside_a_write_panics() {
        let _outer = BorrowGuard::acquire(A, GREETING, true);
        let message = panic_of(|| { BorrowGuard::acquire(A, GREETING, false); }).expect("should panic");
        assert!(message.ends_with("is already borrowed mutably"), "{message}");
    }

    #[test]
    fn other_entities_and_components_dont_conflict() {
        let _a = BorrowGuard::acquire(A, GREETING, true);
        let _b = BorrowGuard::acquire(B, GREETING, true);
        let _c = BorrowGuard::acquire(A, HEALTH, true);
        assert_eq!(active(), 3);
    }

    #[test]
    fn a_finished_borrow_is_released() {
        { let _read = BorrowGuard::acquire(A, GREETING, false); }
        { let _write = BorrowGuard::acquire(A, GREETING, true); }
        let _again = BorrowGuard::acquire(A, GREETING, true);
        assert_eq!(active(), 1);
    }

    #[test]
    fn dropping_releases_only_its_own_borrow() {
        let _read = BorrowGuard::acquire(A, GREETING, false);
        { let _other_read = BorrowGuard::acquire(A, GREETING, false); }
        assert_eq!(active(), 1);
        assert!(panic_of(|| { BorrowGuard::acquire(A, GREETING, true); }).is_some(), "the first read is still active");
    }

    #[test]
    fn a_panic_inside_the_borrow_releases_it() {
        // What `guard` sees: the system panics while it holds the component.
        assert!(panic_of(|| {
            let _write = BorrowGuard::acquire(A, GREETING, true);
            panic!("the system failed");
        }).is_some());
        assert_eq!(active(), 0);
        let _next_frame = BorrowGuard::acquire(A, GREETING, true);
    }

    #[test]
    fn the_panic_points_at_the_caller() {
        // Through #[track_caller], the location is this call, not the panic! inside acquire.
        let _outer = BorrowGuard::acquire(A, GREETING, true);
        let (location, line) = (panic_location(|| { BorrowGuard::acquire(A, GREETING, true); }), line!());
        assert_eq!(location, Some((file!().to_owned(), line)));
    }

    /// Where `f` panicked, read from the panic info (the payload doesn't carry it). The hook is
    /// global, so it ignores the panics of the other tests running meanwhile.
    fn panic_location(f: impl FnOnce()) -> Option<(String, u32)> {
        use std::sync::Mutex;
        static LOCATION: Mutex<Option<(String, u32)>> = Mutex::new(None);
        let this_test = std::thread::current().id();
        std::panic::set_hook(Box::new(move |info| {
            if std::thread::current().id() == this_test {
                *LOCATION.lock().unwrap() = info.location().map(|l| (l.file().to_owned(), l.line()));
            }
        }));
        let panicked = catch_unwind(AssertUnwindSafe(f)).is_err();
        let _ = std::panic::take_hook();   // back to the default hook
        let location = LOCATION.lock().unwrap().take();
        panicked.then_some(location).flatten()
    }
}
