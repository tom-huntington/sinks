# Sinks
This was an idea I explored for my main project, gave up on it because the pipeline in my main project is enormous, on but still believe has potential.

It's a push based iteration intended for asynchronous reactive programming, based on RxCppv3 and Barry's [Rivers](https://github.com/brevzin/rivers) library.

The idea was to get the benefits of [range-v3](https://github.com/ericniebler/range-v3), specifically type deduction. For example, `buffer_for_signal` instantiates `std::queue<int>`

```cpp
auto [pipeline, signal] = Identity<int>{}
    .map([](auto a) { return a + 3; })
    .buffer_for_signal()
    .sink(println)
    ;

pipeline.push(1);
signal.signal();
```
but we can modify the type returned by map to float
```cpp
auto [pipeline, signal] = Identity<int>{}
    .map([](auto a) { return float(a) / 2; })
    .buffer_for_signal()
    .sink(println)
    ;
```
without having to update the type of the `std::queue` owned by `buffer_for_signal`.

I looked into using RxCpp-v2 but thought `observables` where redundant. You want to consume your reactive pipeline by pushing values to `receivers` (which I have called sinks) without having to make an observable which just encapsulates a receiver. Observables are replaced by `Identity<T>` which serves the purpose of instantiating the types owned by the pipeline (i.e. `std::queue`).

```haskell
-- Category theory for programmers contravariant functors
class Contravariant f where
    contramap :: (b->a) -> (f a->f b)

instance Contravariant(Op r) where
    -- (b->a)->Op r a->Op r b
    contramap f g = g.f
```

The types are instantiated by composing partially applied `contramap`s of the Op functor. The result of this composition is then given the terminating sink allowing contramap to be fully applied, which produces the sink encapsulating the entire pipeline.
