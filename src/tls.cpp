#include <iostream>
#include <numeric>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <condition_variable>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/thread.hpp>
#include <boost/make_shared.hpp>

using namespace std::chrono;

typedef std::list<system_clock::duration> DurationList;
typedef std::vector<DurationList> XDurationList;

thread_local DurationList reset_times_list;
thread_local DurationList set_times_list;
thread_local DurationList get_times_list;

class GrabStatistics
{
public:
    GrabStatistics(DurationList& con):con_(con), start_(system_clock::now())
    {}

    ~GrabStatistics()
    { con_.push_back(system_clock::now() - start_); }
private:
    DurationList& con_;
    system_clock::time_point start_;
};

class TrivialType
{
public:
    TrivialType(int i = 0):i_(i){}

    void set(int i)
    { i_ = i; }

    int get() const
    { return i_; }

private:
    int i_;
};

class NonTrivialType
{
public:
    NonTrivialType(const std::string& value = std::string()):value_(value){}
    ~NonTrivialType()
    {
        value_ += "important string";
    }

    void set(const std::string& value)
    { value_ = value; }

    const std::string& get() const
    { return value_; }

private:
    std::string value_;
};

thread_local boost::shared_ptr<TrivialType> var_cpp11x_tls;
thread_local boost::shared_ptr<NonTrivialType> dvar_cpp11x_tls;

thread_local std::auto_ptr<TrivialType> light_var_cpp11x_tls;
thread_local std::auto_ptr<NonTrivialType> light_dvar_cpp11x_tls;

boost::thread_specific_ptr<TrivialType> var_boost_tls;
boost::thread_specific_ptr<NonTrivialType> dvar_boost_tls;

template <typename>
struct BoostOrSTL;
struct TagBoost;
struct TagCpp11x;
struct TagCpp11xLight;

template <>
struct BoostOrSTL<TagCpp11x>
{
    template <typename Type>
    struct tls_var_type
    { typedef boost::shared_ptr<Type> type; };

    static tls_var_type<TrivialType>::type& trivial() { return var_cpp11x_tls; }
    static tls_var_type<NonTrivialType>::type& nontrivial() { return dvar_cpp11x_tls; }
};

template <>
struct BoostOrSTL<TagCpp11xLight>
{
    template <typename Type>
    struct tls_var_type
    { typedef std::auto_ptr<Type> type; };

    static tls_var_type<TrivialType>::type& trivial() { return light_var_cpp11x_tls; }
    static tls_var_type<NonTrivialType>::type& nontrivial() { return light_dvar_cpp11x_tls; }
};

template <>
struct BoostOrSTL<TagBoost>
{
    template <typename Type>
    struct tls_var_type
    { typedef boost::thread_specific_ptr<Type> type; };

    static tls_var_type<TrivialType>::type& trivial() { return var_boost_tls; }
    static tls_var_type<NonTrivialType>::type& nontrivial() { return dvar_boost_tls; }
};

template <typename Type, typename Realisation>
struct TlsVarAccessor;

template <typename Realisation>
struct TlsVarAccessor<TrivialType, Realisation>
{
    typedef BoostOrSTL<Realisation> TheRealisation;
    typedef typename TheRealisation::template tls_var_type<TrivialType>::type TlsVarType;

    static TlsVarType& get()
    { return TheRealisation::trivial(); }

    static int make_arg(int i) {return i;}
};

template <typename Realisation>
struct TlsVarAccessor<NonTrivialType, Realisation>
{
    typedef BoostOrSTL<Realisation> TheRealisation;
    typedef typename TheRealisation::template tls_var_type<NonTrivialType>::type TlsVarType;

    static TlsVarType& get()
    { return TheRealisation::nontrivial(); }

    static std::string make_arg(int i)
    { std::stringstream ss; ss << i; return ss.str(); }
};

void show_results(XDurationList& dcon, const std::string& end = std::string("\n"))
{
    size_t most_averagel = 0;
    size_t most_min = std::numeric_limits<size_t>::max();
    size_t most_max = 0;

    for(size_t i=0; i<dcon.size(); i++)
    {
        dcon[0].sort();
        system_clock::duration total(0);
        most_averagel += (std::accumulate(dcon[0].begin(), dcon[0].end(), total).count() / dcon[0].size());

        most_min = std::min(most_min, static_cast<size_t>((*dcon[0].begin()).count()));
        most_max = std::max(most_max, static_cast<size_t>((*dcon[0].rbegin()).count()));
    }
    most_averagel /= dcon.size();

    std::stringstream ss;
    ss << "min = " << most_min << "ns\t"
       << "avg = " << most_averagel << "ns\t"
       << "max = " << most_max << "ns" << end;

    std::cout << ss.str();
}

template <typename Type, typename Realisation>
void tls_reset(size_t repeat, DurationList& res)
{
    typedef TlsVarAccessor<Type, Realisation> ConcreteTls;
    for(size_t i=0; i<repeat; i++)
    {
        GrabStatistics s(res);
        ConcreteTls::get().reset(new Type(ConcreteTls::make_arg(i)));
    }
}


template <typename Type, typename Realisation>
void tls_set(size_t repeat, DurationList& res)
{
    typedef TlsVarAccessor<Type, Realisation> ConcreteTls;
    ConcreteTls::get().reset(new Type);

    for(size_t i=0; i<repeat; i++)
    {
        GrabStatistics s(res);
        ConcreteTls::get()->set(ConcreteTls::make_arg(i));
    }
}

template <typename Type, typename Realisation>
void tls_get(size_t repeat, DurationList& res)
{
    typedef TlsVarAccessor<Type, Realisation> ConcreteTls;
    ConcreteTls::get().reset(new Type);

    for(size_t i=0; i<repeat; i++)
    {
        GrabStatistics s(res);
        ConcreteTls::get()->get();
    }
}

class ThreadGroup
{
public:
    template <typename Func>
    void add(Func f)
    {
        threads_.push_back(std::make_shared<std::thread>(f));
    }

    void join_all()
    {
        std::for_each(threads_.begin(), threads_.end(), [](std::shared_ptr<std::thread>& th) { th->join(); });
    }

private:
    std::vector<std::shared_ptr<std::thread> > threads_;
};

template <typename Func>
void check_in_X_thread(size_t count, size_t iterations, Func f)
{
    XDurationList xlist(count);

    ThreadGroup group;
    for (size_t i = 0; i < count; i++)
        group.add(std::bind(f, iterations, boost::ref(xlist[i])));

    group.join_all();
    show_results(xlist);
}

template <typename Func>
void semaphore_wrapper(Func f, std::mutex& mtx, std::condition_variable& cv, size_t& semafore)
{
    f(); // <- concrete test function

    std::unique_lock<std::mutex> lck(mtx);
    --semafore;
    cv.notify_all();
}

/*
 * Support 'simultaneously' threads at the same time while will not be created 'count' threads
 */
template <typename Func>
void check_periodic_with_X_thread(size_t simultaneously, size_t total, size_t iterations, Func f)
{
    typedef boost::function<void()> auto_f;
    assert(simultaneously <= total);

    XDurationList xlist(total);

    std::mutex mtx;
    std::condition_variable cv;
    size_t semafore = 0;

    ThreadGroup group;
    system_clock::time_point start = system_clock::now();

    // create a few first threads
    for (size_t i = 0; i < simultaneously; i++)
    {
        { std::unique_lock<std::mutex> lck(mtx); ++semafore; }
        auto_f fu = std::bind(f, iterations, boost::ref(xlist[i]));
        group.add(std::bind(semaphore_wrapper<auto_f>, fu, boost::ref(mtx), boost::ref(cv), boost::ref(semafore)));
    }

    // support is not less than X threads
    for(size_t i = simultaneously; i < total; i++)
    {
        // wait while on of the thread will finish
        {
            std::unique_lock<std::mutex> lck(mtx);
            while (semafore >= simultaneously) cv.wait(lck);
            ++semafore;
        }

        // add new one
        auto_f fu = std::bind(f, iterations, boost::ref(xlist[i]));
        group.add(std::bind(semaphore_wrapper<auto_f>, fu, boost::ref(mtx), boost::ref(cv), boost::ref(semafore)));
    }

    group.join_all();

    system_clock::duration dur = system_clock::now() - start;
    std::stringstream ss;
    ss << "    \t" << "total spent = " << duration_cast<milliseconds>(dur).count() << "ms\t";
    ss << "per th = " << duration_cast<milliseconds>(dur).count() * 1.0 / total << "ms" << std::endl;

    show_results(xlist, ss.str());
}

void say(const std::string& what, const std::string& separator = std::string(":"))
{
    std::cout << what << separator;
}

template <typename Func>
void test(Func test_function)
{
    std::cout << "For TrivialType:\n";
    say("[reset]", "\n");
    { say("c++11x"); test_function(boost::bind(tls_reset<TrivialType, TagCpp11x>, _1, _2)); }
    { say("boost"); test_function(boost::bind(tls_reset<TrivialType, TagBoost>, _1, _2)); }
    { say("stdLig"); test_function(boost::bind(tls_reset<TrivialType, TagCpp11xLight>, _1, _2)); }

    say("[set]", "\n");
    { say("c++11x"); test_function(boost::bind(tls_set<TrivialType, TagCpp11x>, _1, _2)); }
    { say("boost"); test_function(boost::bind(tls_set<TrivialType, TagBoost>, _1, _2)); }
    { say("stdLig"); test_function(boost::bind(tls_set<TrivialType, TagCpp11xLight>, _1, _2)); }

    say("[get]", "\n");
    { say("c++11x"); test_function(boost::bind(tls_get<TrivialType, TagCpp11x>, _1, _2)); }
    { say("boost"); test_function(boost::bind(tls_get<TrivialType, TagBoost>, _1, _2)); }
    { say("stdLig"); test_function(boost::bind(tls_get<TrivialType, TagCpp11xLight>, _1, _2)); }

    std::cout << "For NonTrivialType:\n";
    say("[reset]", "\n");
    { say("c++11x"); test_function(boost::bind(tls_reset<NonTrivialType, TagCpp11x>, _1, _2)); }
    { say("boost"); test_function(boost::bind(tls_reset<NonTrivialType, TagBoost>, _1, _2)); }
    { say("c11xL"); test_function(boost::bind(tls_reset<NonTrivialType, TagCpp11xLight>, _1, _2)); }

    say("[set]", "\n");
    { say("c++11x"); test_function(boost::bind(tls_set<NonTrivialType, TagCpp11x>, _1, _2)); }
    { say("boost"); test_function(boost::bind(tls_set<NonTrivialType, TagBoost>, _1, _2)); }
    { say("c11xL"); test_function(boost::bind(tls_set<NonTrivialType, TagCpp11xLight>, _1, _2)); }

    say("[get]", "\n");
    { say("c++11x"); test_function(boost::bind(tls_get<NonTrivialType, TagCpp11x>, _1, _2)); }
    { say("boost"); test_function(boost::bind(tls_get<NonTrivialType, TagBoost>, _1, _2)); }
    { say("c11xL"); test_function(boost::bind(tls_get<NonTrivialType, TagCpp11xLight>, _1, _2)); }
}

int main(int argc, const char* argv[])
{
    size_t thread_multiplicator = 1;
    size_t total_threads = 1;
    size_t iterations = 100000;
    bool periodic_thread = false;

    if(argc >= 2)
        thread_multiplicator = std::stoul(argv[1]);

    if(argc >= 3)
        iterations = std::stoul(argv[2]);

    if(argc >= 5)
    {
        periodic_thread = (argv[3] == std::string("periodic"));
        total_threads = std::stoul(argv[4]);
    }

    typedef boost::function<void(size_t, DurationList&)> FinalTestFunction;
    if(!periodic_thread)
    {
        std::cout << "Start test: " << thread_multiplicator << "x" << iterations << std::endl;
        boost::function<void(FinalTestFunction)> f =
                boost::bind(check_in_X_thread<FinalTestFunction>, thread_multiplicator, iterations, _1);
        test(f);
    }
    else
    {
        std::cout << "Start test: " << thread_multiplicator << "x" << iterations  << " all threads = " << total_threads << std::endl;
        boost::function<void(FinalTestFunction)> f =
                boost::bind(check_periodic_with_X_thread<FinalTestFunction>, thread_multiplicator, total_threads, iterations, _1);
        test(f);
    }
}

