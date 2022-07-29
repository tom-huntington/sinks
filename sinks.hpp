#include <iostream>
#include <mutex>
#include <memory>
#include <queue>
#include <array>
#include <ranges>

#define RVR_FWD(x) static_cast<decltype(x)&&>(x)
#define RVR_RETURNS(e) -> decltype(e) { return e; }

template <typename T>
using downstream_ref_t = typename std::remove_cvref_t<T>::downstream_ref;
template <typename T>
using upstream_ref_t = typename std::remove_cvref_t<T>::upstream_ref;

template<typename S, typename T, typename ...Types>
auto set_first(std::tuple<T, Types...> tuple, S sink) -> std::tuple<S, Types...>
{
	return std::apply([&](auto head, auto... tail) {
			return std::make_tuple(sink, tail...); }
		, std::move(tuple));
}

template<typename S, typename T, typename ...Types>
auto set_first_pair(S sink, std::tuple<T, Types...> tuple) -> std::tuple<S, S, Types...>
{
	return std::apply([&](auto head, auto... tail) {
		return std::make_tuple(sink, sink, tail...); }
	, std::move(tuple));
}

template <typename F>
struct Sink
{
	Sink(F f) : f_{ f } {}
	F f_;
	void push(auto v) { f_(v); }
};
template <typename F>
struct Signal
{
	Signal(F f) : f_{ f } {}
	F f_;
	void signal() { f_(); }
};


// Category theory for programmers contravariant functors
//
//class Contravariant f where
//    contramap :: (b->a) -> (f a->f b)
//
//instance Contravariant(Op r) where
//    -- (b->a)->Op r a->Op r b
//    contramap f g = g.f


template <typename Derived>
struct OpBase
{
	template <typename F> constexpr auto flat_map(F && f)&;
	template <typename F> constexpr auto flat_map(F && f)&&;
	
	template <typename F> constexpr auto map(F && f)&;
	template <typename F> constexpr auto map(F && f)&&;

	constexpr auto branch()&;
	constexpr auto branch()&&;

	constexpr auto buffer_for_signal()&;
	constexpr auto buffer_for_signal()&&;

	template <typename F> constexpr auto sink(F f) { return self().contramap(Sink{ std::move(f) }); }
private:
	constexpr auto self() -> Derived& { return *(Derived*)this; }
};


template <typename UpstreamOp, typename F>
	requires std::regular_invocable<F&, downstream_ref_t<UpstreamOp>>
struct Map : OpBase<Map<UpstreamOp, F>>
{
	using upstream_ref = downstream_ref_t<UpstreamOp>;
	using downstream_ref = std::invoke_result_t<F&, upstream_ref>;

	UpstreamOp upstream_op_;
	F f_;//upstream_ref -> downstream_ref 
	
	constexpr Map(UpstreamOp upstream_op, F f) : upstream_op_(std::move(upstream_op)), f_(std::move(f)) { }

	template <typename down_t>
	auto contramap(Sink<down_t> downstream_sink)
	{
		return upstream_op_.contramap(
			Sink{ [=](upstream_ref v) mutable {
				downstream_sink.push(f_(v));
			} });
	};
	template <typename ...Types>
	auto contramap(std::tuple<Types...> downstream_sinks)
	{
		auto downstream_sink = std::get<0>(downstream_sinks);
		return upstream_op_.contramap(
			set_first(downstream_sinks, 
				Sink{ [=](upstream_ref v) mutable { downstream_sink.push(f_(v));}}
			)
		);
	};
};


template <typename Derived>
template <typename F>
constexpr auto OpBase<Derived>::map(F&& f)&& {
	static_assert(std::invocable<F&, downstream_ref_t<Derived>>);
	return Map(RVR_FWD(self()), RVR_FWD(f));
}
template <typename Derived>
template <typename F>
constexpr auto OpBase<Derived>::map(F&& f)& {
	static_assert(std::invocable<F&, downstream_ref_t<Derived>>);
	return Map(RVR_FWD(self()), RVR_FWD(f));
}
template <typename UpstreamOp, typename F>
	requires std::regular_invocable<F&, downstream_ref_t<UpstreamOp>>
struct FlatMap : OpBase<FlatMap<UpstreamOp, F>>
{
	using upstream_ref = downstream_ref_t<UpstreamOp>;
	using downstream_ref = std::ranges::range_value_t<std::invoke_result_t<F&, upstream_ref>>;

	UpstreamOp upstream_op_;
	F f_;//upstream_ref -> downstream_ref 
	
	constexpr FlatMap(UpstreamOp upstream_op, F f) : upstream_op_(std::move(upstream_op)), f_(std::move(f)) { }

	template <typename down_t>
	auto contramap(Sink<down_t> downstream_sink)
	{
		return upstream_op_.contramap(
			Sink{ [=](upstream_ref v) mutable {
				for(auto&& e: f_(v))
					downstream_sink.push(std::move(e));
			} });
	};
	template <typename ...Types>
	auto contramap(std::tuple<Types...> downstream_sinks)
	{
		auto downstream_sink = std::get<0>(downstream_sinks);
		return upstream_op_.contramap(
			set_first(downstream_sinks,
				Sink{ [=](upstream_ref v) mutable {
					for (auto&& e : f_(v))
						downstream_sink.push(std::move(e));
							}
				}

			)
		);
	};
};


template <typename Derived>
template <typename F>
constexpr auto OpBase<Derived>::flat_map(F&& f)&& {
	static_assert(std::invocable<F&, downstream_ref_t<Derived>>);
	return FlatMap(RVR_FWD(self()), RVR_FWD(f));
}
template <typename Derived>
template <typename F>
constexpr auto OpBase<Derived>::flat_map(F&& f)& {
	static_assert(std::invocable<F&, downstream_ref_t<Derived>>);
	return FlatMap(RVR_FWD(self()), RVR_FWD(f));
}


template <typename UpstreamOp>
struct Branch : OpBase<Branch<UpstreamOp>>
{
	using upstream_ref = downstream_ref_t<UpstreamOp>;
	using downstream_ref = upstream_ref;

	UpstreamOp upstream_op_;
	
	constexpr Branch(UpstreamOp upstream_op) : upstream_op_(std::move(upstream_op)) { }

	template <typename down_t>
	auto contramap(Sink<down_t> downstream_sink)
	{
		return upstream_op_.contramap(
			std::tuple{ 
				Sink{ [=](upstream_ref v) mutable { downstream_sink.push(v); } },
				Sink{ [=](upstream_ref v) mutable { downstream_sink.push(v); } } 
			});
	};
	
	template <typename ...Types>
	auto contramap(std::tuple<Types...> downstream_sinks)
	{
		auto downstream_sink = std::get<0>(downstream_sinks);
		return upstream_op_.contramap(
			set_first_pair(
				Sink{ [=](upstream_ref v) mutable { downstream_sink.push(v); } }, downstream_sinks)
			);
	};
};
template <typename Derived>
constexpr auto OpBase<Derived>::branch()&& {
	return Branch(RVR_FWD(self()));
}
template <typename Derived>
constexpr auto OpBase<Derived>::branch()& {
	return Branch(RVR_FWD(self()));
}
template <typename UpstreamOp>
struct BufferForSignal : OpBase<BufferForSignal<UpstreamOp>>
{
	using upstream_ref = downstream_ref_t<UpstreamOp>;
	using downstream_ref = upstream_ref;

	UpstreamOp upstream_op_;
	
	constexpr BufferForSignal(UpstreamOp upstream_op) : upstream_op_(std::move(upstream_op)) { }

	template <typename down_t>
	auto contramap(Sink<down_t> downstream_sink)
	{
		auto p = std::make_shared<std::pair<std::queue<upstream_ref>, std::mutex>>();
		return upstream_op_.contramap(
			std::tuple{ 
				Sink{ [=](upstream_ref v) mutable { 
					auto& [queue, mutex] = *p;
					const auto lock = std::lock_guard(mutex);
					queue.push(v);
				} },
				Signal{ [=]() mutable { 
					auto& [queue, mutex] = *p;
					const auto lock = std::lock_guard(mutex);
					while (queue.size())
					{
						downstream_sink.push(queue.front());
						queue.pop();
					}
				} } 
			});
	};
	
	template <typename ...Types>
	auto contramap(std::tuple<Types...> downstream_sinks)
	{
		auto downstream_sink = std::get<0>(downstream_sinks);
		return upstream_op_.contramap(
			set_first_pair(
				Sink{ [=](upstream_ref v) mutable { downstream_sink.push(v); } }, downstream_sinks)
			);
	};
};
template <typename Derived>
constexpr auto OpBase<Derived>::buffer_for_signal()&& {
	return BufferForSignal(RVR_FWD(self()));
}
template <typename Derived>
constexpr auto OpBase<Derived>::buffer_for_signal()& {
	return Branch(RVR_FWD(self()));
}




template <typename T>
struct Identity: OpBase<Identity<T>>
{
	using upstream_ref = T;
	using downstream_ref = T;

	template <typename down_t>
	auto contramap(Sink<down_t> downstream_sink)
	{
			return Sink{ [=](upstream_ref v) mutable { downstream_sink.push(v); } };
	}
	template <typename ...Types>
	auto contramap(std::tuple<Types...> downstream_sinks)
	{
		auto downstream_sink = std::get<0>(downstream_sinks);
		return set_first(downstream_sinks,
				Sink{ [=](upstream_ref v) mutable { downstream_sink.push(v); } }
		);
	}
};
