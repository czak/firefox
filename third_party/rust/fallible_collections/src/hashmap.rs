//! Implement Fallible HashMap
use super::TryClone;
use crate::TryReserveError;
use core::borrow::Borrow;
use core::default::Default;
use core::fmt::Debug;
use core::hash::Hash;

#[cfg(not(feature = "std"))]
type HashMap<K, V> = hashbrown::hash_map::HashMap<K, V>;

#[cfg(feature = "std")]
type HashMap<K, V> = std::collections::HashMap<K, V>;

#[cfg(not(feature = "std"))]
use hashbrown::hash_map::{IntoIter, Iter};

#[cfg(feature = "std")]
use std::collections::hash_map::{IntoIter, Iter};

pub struct TryHashMap<K, V> {
    inner: HashMap<K, V>,
}

impl<K, V> Default for TryHashMap<K, V> {
    #[inline(always)]
    fn default() -> Self {
        Self {
            inner: Default::default(),
        }
    }
}

impl<K: Debug, V: Debug> Debug for TryHashMap<K, V> {
    #[inline]
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        self.inner.fmt(f)
    }
}

impl<K, V> TryHashMap<K, V>
where
    K: Eq + Hash,
{
    #[inline]
    pub fn with_capacity(capacity: usize) -> Result<Self, TryReserveError> {
        let mut map = Self {
            inner: HashMap::new(),
        };
        map.reserve(capacity)?;
        Ok(map)
    }

    #[inline(always)]
    pub fn get<Q: ?Sized>(&self, k: &Q) -> Option<&V>
    where
        K: Borrow<Q>,
        Q: Hash + Eq,
    {
        self.inner.get(k)
    }

    #[inline]
    pub fn insert(&mut self, k: K, v: V) -> Result<Option<V>, TryReserveError> {
        self.reserve(if self.inner.capacity() == 0 { 4 } else { 1 })?;
        Ok(self.inner.insert(k, v))
    }

    #[inline(always)]
    pub fn iter(&self) -> Iter<'_, K, V> {
        self.inner.iter()
    }

    #[inline(always)]
    pub fn len(&self) -> usize {
        self.inner.len()
    }

    #[inline(always)]
    pub fn remove<Q: ?Sized>(&mut self, k: &Q) -> Option<V>
    where
        K: Borrow<Q>,
        Q: Hash + Eq,
    {
        self.inner.remove(k)
    }

    #[inline(always)]
    fn reserve(&mut self, additional: usize) -> Result<(), TryReserveError> {
        self.inner
            .try_reserve(additional)
            .map_err(|_| make_try_reserve_error())
    }
}

impl<K, V> IntoIterator for TryHashMap<K, V> {
    type Item = (K, V);
    type IntoIter = IntoIter<K, V>;

    #[inline(always)]
    fn into_iter(self) -> Self::IntoIter {
        self.inner.into_iter()
    }
}

impl<K, V> TryClone for TryHashMap<K, V>
where
    K: Eq + Hash + TryClone,
    V: TryClone,
{
    fn try_clone(&self) -> Result<Self, TryReserveError> {
        let mut clone = Self::with_capacity(self.inner.len())?;

        for (key, value) in self.inner.iter() {
            clone.insert(key.try_clone()?, value.try_clone()?)?;
        }

        Ok(clone)
    }
}

#[test]
fn tryhashmap_oom() {
    match TryHashMap::<char, char>::default().reserve(core::usize::MAX) {
        Ok(_) => panic!("it should be OOM"),
        _ => (),
    }
}

#[cold]
fn make_try_reserve_error() -> TryReserveError {
    let mut v: alloc::vec::Vec<[u8; 1024]> = alloc::vec::Vec::new();
    // this will always overflow capacity
    v.try_reserve(!0).unwrap_err()
}
