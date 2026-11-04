use std::{marker::PhantomData, ops::{Deref, DerefMut}};

use crate::{interop::host::host, scripting::{borrow::BorrowGuard, component::Component, system::Context, world::ObjectID}};

#[derive(Debug)]
pub struct Query<'a, T: Component> {
    _ctx: &'a Context,
    _marker: PhantomData<T>
}

pub struct Ref<'a, T> { value: &'a T, _borrow: BorrowGuard }
pub struct RefMut<'a, T> { value: &'a mut T, _borrow: BorrowGuard }

impl<'a, T> Deref for Ref<'a, T> {
    type Target = T;

    fn deref(&self) -> &T {
        self.value
    }
}

impl<'a, T> Deref for RefMut<'a, T> {
    type Target = T;

    fn deref(&self) -> &T {
        self.value
    }
}

impl<'a, T> DerefMut for RefMut<'a, T> {
    fn deref_mut(&mut self) -> &mut T {
        self.value
    }
}

impl<'a, T: Component> Query<'a, T> {
    pub fn new(_ctx: &'a Context) -> Self {
        let _marker = PhantomData;
        Self { _ctx, _marker }
    }

    #[track_caller] pub fn get(&self, entity: ObjectID) -> Option<Ref<'a, T>> {
        let ptr = component_ptr::<T>(entity)?;
        // Before the reference exists: a conflict must panic before there are two of them.
        let _borrow = BorrowGuard::acquire(entity, T::ID, false);
        let component = unsafe { ptr.as_ref() }?;
        Some(Ref { value: component, _borrow })
    }

    #[track_caller] pub fn get_mut(&self, entity: ObjectID) -> Option<RefMut<'a, T>> {
        let ptr = component_ptr::<T>(entity)? as *mut T;
        // Before the reference exists: a conflict must panic before there are two of them.
        let _borrow = BorrowGuard::acquire(entity, T::ID, true);
        let component = unsafe { ptr.as_mut() }?;
        Some(RefMut { value: component, _borrow })
    }
}

pub(crate) fn component_ptr<T: Component>(entity: ObjectID) -> Option<*const T> {
    let host = host()?;
    let ptr = (host.get_component)(host.host, entity, T::ID) as *const T;
    Some(ptr)
}