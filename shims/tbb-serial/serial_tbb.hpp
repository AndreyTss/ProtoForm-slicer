// Однопоточная замена oneTBB для сборки libslic3r без потоков.
//
// Покрывает ровно ту часть API oneTBB 2021, которой пользуются
// orca/src/libslic3r и orca/deps_src. Всё выполняется в вызывающем потоке
// по порядку. Это одно из расписаний, которые настоящий TBB вправе выбрать,
// поэтому результаты совпадают с возможными результатами TBB.
//
// Файлы tbb/*.h и oneapi/tbb/*.h рядом только подключают этот заголовок:
// одна точка правды проще, чем два десятка мелких файлов.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

// libslic3r выбирает API по этому номеру (GCode.cpp, utils.cpp): с меньшим
// он подключил бы tbb/pipeline.h и task_scheduler_init.h старого TBB.
#define TBB_VERSION_MAJOR 2021
#define TBB_VERSION_MINOR 12
#define TBB_INTERFACE_VERSION 12120
#define TBB_SERIAL_SHIM 1

namespace oneapi {
namespace tbb {

// ---- Диапазоны ---------------------------------------------------------------

class split {};

template<typename Value>
class blocked_range
{
public:
    using value_type     = Value;
    using const_iterator = Value;
    using size_type      = std::size_t;

    blocked_range() = default;
    blocked_range(Value begin, Value end, size_type grainsize = 1)
        : m_begin(begin), m_end(end), m_grainsize(grainsize)
    {}

    const_iterator begin() const { return m_begin; }
    const_iterator end() const { return m_end; }
    size_type      size() const { return size_type(m_end - m_begin); }
    size_type      grainsize() const { return m_grainsize; }
    bool           empty() const { return !(m_begin < m_end); }
    // Диапазон никогда не делится: всё тело получает его целиком.
    bool           is_divisible() const { return false; }

private:
    Value     m_begin{};
    Value     m_end{};
    size_type m_grainsize = 1;
};

template<typename RowValue, typename ColValue = RowValue>
class blocked_range2d
{
public:
    using row_range_type = blocked_range<RowValue>;
    using col_range_type = blocked_range<ColValue>;

    blocked_range2d(RowValue row_begin, RowValue row_end, std::size_t row_grain,
                    ColValue col_begin, ColValue col_end, std::size_t col_grain)
        : m_rows(row_begin, row_end, row_grain), m_cols(col_begin, col_end, col_grain)
    {}
    blocked_range2d(RowValue row_begin, RowValue row_end, ColValue col_begin, ColValue col_end)
        : m_rows(row_begin, row_end), m_cols(col_begin, col_end)
    {}

    const row_range_type &rows() const { return m_rows; }
    const col_range_type &cols() const { return m_cols; }
    bool empty() const { return m_rows.empty() || m_cols.empty(); }
    bool is_divisible() const { return false; }

private:
    row_range_type m_rows;
    col_range_type m_cols;
};

// Разбиение работы между потоками здесь ни на что не влияет.
class simple_partitioner {};
class auto_partitioner {};
class static_partitioner {};
class affinity_partitioner {};

namespace detail {
// Отличает диапазон от индекса: у диапазона есть is_divisible(). Без этого
// parallel_for(0, n, f) мог бы выбрать форму «диапазон, тело, разбиение».
template<typename T, typename = void>
struct is_range : std::false_type {};
template<typename T>
struct is_range<T, std::void_t<decltype(std::declval<const T &>().is_divisible())>> : std::true_type {};
} // namespace detail

// ---- Параллельные алгоритмы ---------------------------------------------------

template<typename Range, typename Body,
         typename = std::enable_if_t<detail::is_range<Range>::value>>
void parallel_for(const Range &range, const Body &body) { body(range); }

template<typename Range, typename Body, typename Partitioner,
         typename = std::enable_if_t<detail::is_range<Range>::value>>
void parallel_for(const Range &range, const Body &body, Partitioner &&) { body(range); }

template<typename Index, typename Function,
         typename = std::enable_if_t<!detail::is_range<Index>::value>>
void parallel_for(Index first, Index last, const Function &f)
{
    for (Index i = first; i < last; ++i)
        f(i);
}

template<typename Index, typename Function, typename Partitioner,
         typename = std::enable_if_t<!detail::is_range<Index>::value &&
                                     !std::is_convertible_v<Function, Index>>>
void parallel_for(Index first, Index last, const Function &f, Partitioner &&)
{
    parallel_for(first, last, f);
}

template<typename Index, typename Function,
         typename = std::enable_if_t<!detail::is_range<Index>::value>>
void parallel_for(Index first, Index last, Index step, const Function &f)
{
    for (Index i = first; i < last; i += step)
        f(i);
}

// Функциональная форма: тело получает весь диапазон и начальное значение.
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce(const Range &range, const Value &identity, const RealBody &body, const Reduction &)
{
    return body(range, identity);
}

template<typename Range, typename Value, typename RealBody, typename Reduction, typename Partitioner>
Value parallel_reduce(const Range &range, const Value &identity, const RealBody &body, const Reduction &,
                      Partitioner &&)
{
    return body(range, identity);
}

// Императивная форма: тело само копит результат, join не нужен.
template<typename Range, typename Body>
void parallel_reduce(const Range &range, Body &body) { body(range); }

template<typename Range, typename Body, typename Partitioner>
void parallel_reduce(const Range &range, Body &body, Partitioner &&) { body(range); }

template<typename Iterator, typename Function>
void parallel_for_each(Iterator first, Iterator last, const Function &f)
{
    for (; first != last; ++first)
        f(*first);
}

template<typename Container, typename Function>
void parallel_for_each(Container &c, const Function &f)
{
    for (auto &item : c)
        f(item);
}

template<typename Iterator>
void parallel_sort(Iterator first, Iterator last) { std::sort(first, last); }

template<typename Iterator, typename Compare>
void parallel_sort(Iterator first, Iterator last, const Compare &comp) { std::sort(first, last, comp); }

template<typename Container>
void parallel_sort(Container &c) { std::sort(std::begin(c), std::end(c)); }

// ---- Конвейер (GCode.cpp пишет G-код через parallel_pipeline) -----------------

enum class filter_mode { parallel, serial_in_order, serial_out_of_order };

class flow_control
{
public:
    void stop() { m_stopped = true; }
    bool is_stopped() const { return m_stopped; }

private:
    bool m_stopped = false;
};

namespace detail {
// Первая стадия получает flow_control, остальные — результат предыдущей.
template<typename In>
struct stage_input { using type = In; };
template<>
struct stage_input<void> { using type = flow_control &; };

// Источник сказал stop(): остаток цепочки для этого элемента не выполняется.
// Исключение проще, чем протаскивать «пустое значение» через стадии с типами,
// у которых нет конструктора по умолчанию.
struct pipeline_stopped {};
} // namespace detail

template<typename In, typename Out>
class filter
{
public:
    using argument_type = typename detail::stage_input<In>::type;
    using function_type = std::function<Out(argument_type)>;

    filter() = default;
    filter(filter_mode, function_type fn) : m_fn(std::move(fn)) {}

    Out operator()(argument_type arg) const { return m_fn(std::forward<argument_type>(arg)); }

private:
    function_type m_fn;
};

template<typename In, typename Out, typename Body>
filter<In, Out> make_filter(filter_mode mode, const Body &body)
{
    return filter<In, Out>(mode, typename filter<In, Out>::function_type(body));
}

template<typename In, typename Mid, typename Out>
filter<In, Out> operator&(const filter<In, Mid> &first, const filter<Mid, Out> &second)
{
    if constexpr (std::is_void_v<In>) {
        return filter<In, Out>(filter_mode::serial_in_order, [first, second](flow_control &fc) -> Out {
            Mid item = first(fc);
            if (fc.is_stopped())
                throw detail::pipeline_stopped{};
            return second(std::move(item));
        });
    } else {
        return filter<In, Out>(filter_mode::serial_in_order,
                               [first, second](In item) -> Out { return second(first(std::move(item))); });
    }
}

inline void parallel_pipeline(std::size_t /* max_tokens */, const filter<void, void> &chain)
{
    for (;;) {
        flow_control fc;
        try {
            chain(fc);
        } catch (const detail::pipeline_stopped &) {
            return;
        }
        if (fc.is_stopped())
            return;
    }
}

// ---- Задачи и арены -----------------------------------------------------------

enum task_group_status { not_complete, complete, canceled };

// Задача выполняется сразу при run(). Исключение запоминается и бросается
// из wait(), как у TBB.
class task_group
{
public:
    task_group() = default;
    task_group(const task_group &) = delete;
    task_group &operator=(const task_group &) = delete;
    ~task_group() = default;

    template<typename F>
    void run(F &&f)
    {
        if (m_canceled || m_error)
            return;
        try {
            f();
        } catch (...) {
            m_error = std::current_exception();
        }
    }

    task_group_status wait()
    {
        if (m_error) {
            std::exception_ptr e = m_error;
            m_error              = nullptr;
            std::rethrow_exception(e);
        }
        return m_canceled ? canceled : complete;
    }

    template<typename F>
    task_group_status run_and_wait(F &&f)
    {
        run(std::forward<F>(f));
        return wait();
    }

    void cancel() { m_canceled = true; }
    bool is_canceling() const { return m_canceled; }

private:
    bool               m_canceled = false;
    std::exception_ptr m_error;
};

// Задача в task_group выполняется сразу, отменять в середине нечего.
inline bool is_current_task_group_canceling() { return false; }

namespace this_task_arena {
// Ровно 1: Thread.cpp ждёт, пока одновременно запустятся max_concurrency()
// задач, и с любым другим числом в одном потоке ждал бы вечно.
inline int max_concurrency() { return 1; }
inline int current_thread_index() { return 0; }

template<typename F>
auto isolate(F &&f) -> decltype(f()) { return f(); }
} // namespace this_task_arena

class task_arena
{
public:
    static constexpr int automatic = -1;

    explicit task_arena(int /* max_concurrency */ = automatic, unsigned /* reserved */ = 1) {}

    void initialize() {}
    template<typename... Args>
    void initialize(Args &&...) {}
    void terminate() {}
    bool is_active() const { return true; }
    int  max_concurrency() const { return 1; }

    template<typename F>
    auto execute(F &&f) -> decltype(f()) { return f(); }

    template<typename F>
    void enqueue(F &&f) { f(); }
};

class global_control
{
public:
    enum parameter { max_allowed_parallelism, thread_stack_size, terminate_on_exception, scheduler_handle };

    global_control(parameter, std::size_t) {}

    static std::size_t active_value(parameter p) { return p == max_allowed_parallelism ? 1 : 0; }
};

// ---- Синхронизация ------------------------------------------------------------

// Других потоков нет, захватывать нечего.
class spin_mutex
{
public:
    void lock() {}
    bool try_lock() { return true; }
    void unlock() {}

    class scoped_lock
    {
    public:
        scoped_lock() = default;
        explicit scoped_lock(spin_mutex &) {}
        void acquire(spin_mutex &) {}
        bool try_acquire(spin_mutex &) { return true; }
        void release() {}
    };
};

using mutex = spin_mutex;

// ---- Контейнеры ---------------------------------------------------------------

// Как у TBB, адреса элементов не меняются при росте (поэтому deque, а не
// vector), а push_back и grow_by возвращают итераторы.
template<typename T, typename Allocator = std::allocator<T>>
class concurrent_vector
{
    using storage = std::deque<T, Allocator>;

public:
    using value_type      = T;
    using size_type       = typename storage::size_type;
    using reference       = T &;
    using const_reference = const T &;
    using iterator        = typename storage::iterator;
    using const_iterator  = typename storage::const_iterator;

    concurrent_vector() = default;
    explicit concurrent_vector(size_type n) : m_items(n) {}
    concurrent_vector(size_type n, const T &value) : m_items(n, value) {}
    template<typename It>
    concurrent_vector(It first, It last) : m_items(first, last) {}
    concurrent_vector(std::initializer_list<T> init) : m_items(init) {}

    iterator push_back(const T &value)
    {
        m_items.push_back(value);
        return std::prev(m_items.end());
    }
    iterator push_back(T &&value)
    {
        m_items.push_back(std::move(value));
        return std::prev(m_items.end());
    }
    template<typename... Args>
    iterator emplace_back(Args &&...args)
    {
        m_items.emplace_back(std::forward<Args>(args)...);
        return std::prev(m_items.end());
    }
    iterator grow_by(size_type n)
    {
        size_type old = m_items.size();
        m_items.resize(old + n);
        return m_items.begin() + old;
    }
    iterator grow_to_at_least(size_type n)
    {
        if (m_items.size() < n)
            m_items.resize(n);
        return m_items.begin() + n;
    }

    void      reserve(size_type) {}
    void      shrink_to_fit() { m_items.shrink_to_fit(); }
    void      resize(size_type n) { m_items.resize(n); }
    void      clear() { m_items.clear(); }
    size_type size() const { return m_items.size(); }
    bool      empty() const { return m_items.empty(); }

    reference       operator[](size_type i) { return m_items[i]; }
    const_reference operator[](size_type i) const { return m_items[i]; }
    reference       at(size_type i) { return m_items.at(i); }
    const_reference at(size_type i) const { return m_items.at(i); }
    reference       front() { return m_items.front(); }
    reference       back() { return m_items.back(); }

    iterator       begin() { return m_items.begin(); }
    iterator       end() { return m_items.end(); }
    const_iterator begin() const { return m_items.begin(); }
    const_iterator end() const { return m_items.end(); }
    const_iterator cbegin() const { return m_items.cbegin(); }
    const_iterator cend() const { return m_items.cend(); }

private:
    storage m_items;
};

template<typename Key, typename T, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>,
         typename Allocator = std::allocator<std::pair<const Key, T>>>
class concurrent_unordered_map : public std::unordered_map<Key, T, Hash, KeyEqual, Allocator>
{
    using base = std::unordered_map<Key, T, Hash, KeyEqual, Allocator>;

public:
    using base::base;
    typename base::size_type unsafe_erase(const Key &key) { return base::erase(key); }
    typename base::iterator  unsafe_erase(typename base::const_iterator it) { return base::erase(it); }
};

template<typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>,
         typename Allocator = std::allocator<Key>>
class concurrent_unordered_set : public std::unordered_set<Key, Hash, KeyEqual, Allocator>
{
    using base = std::unordered_set<Key, Hash, KeyEqual, Allocator>;

public:
    using base::base;
    typename base::size_type unsafe_erase(const Key &key) { return base::erase(key); }
    typename base::iterator  unsafe_erase(typename base::const_iterator it) { return base::erase(it); }
};

// ---- Память -------------------------------------------------------------------

// Отдельный тип, а не псевдоним std::allocator: тогда Slic3r::Points
// (std::vector с этим распределителем) остаётся другим типом, чем
// std::vector<Point>, и перегрузки libslic3r не сталкиваются.
template<typename T>
class scalable_allocator
{
public:
    using value_type      = T;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;
    using is_always_equal = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;

    template<typename U>
    struct rebind { using other = scalable_allocator<U>; };

    scalable_allocator() noexcept = default;
    template<typename U>
    scalable_allocator(const scalable_allocator<U> &) noexcept {}

    T *allocate(std::size_t n) { return static_cast<T *>(::operator new(n * sizeof(T))); }
    void deallocate(T *p, std::size_t) noexcept { ::operator delete(p); }

    template<typename U>
    bool operator==(const scalable_allocator<U> &) const noexcept { return true; }
    template<typename U>
    bool operator!=(const scalable_allocator<U> &) const noexcept { return false; }
};

} // namespace tbb
} // namespace oneapi

namespace tbb = oneapi::tbb;

// C-функции tbbmalloc (их зовёт clipper2): обычный malloc.
inline void *scalable_malloc(std::size_t size) { return std::malloc(size); }
inline void  scalable_free(void *ptr) { std::free(ptr); }
inline void *scalable_realloc(void *ptr, std::size_t size) { return std::realloc(ptr, size); }
inline void *scalable_calloc(std::size_t n, std::size_t size) { return std::calloc(n, size); }
