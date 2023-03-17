#pragma once
#include <cstring>
#include "Utils/DebugUtils.h"

template<typename FisrtType, typename ...Args>
struct deduce_first
{
    using type = FisrtType;
};
template<typename ...Args>
using deduce_first_t = typename deduce_first<Args...>::type;

struct NonCopyable
{
	NonCopyable() {}
	~NonCopyable() {}
private:
	NonCopyable(const NonCopyable&) = delete;
	NonCopyable& operator=(const NonCopyable&) = delete;
};


template< typename T >
struct array_deleter
{
    void operator ()(T const* p)
    {
        delete[] p;
    }
};

template<typename T>
std::enable_if_t<!std::is_lvalue_reference_v<T>, T*> 
inline make_rvalue_ptr(T&& v) { return &v; }
#define PTR(v) make_rvalue_ptr(v)

template<typename T, size_t N>
inline decltype(auto) make_array_ref(T(&arr)[N]) { return arr; }

template<typename T>
inline std::enable_if_t<!std::is_lvalue_reference_v<T> && !std::is_rvalue_reference_v<T>, void> print_type()
{
    Utility::PrintMessage(typeid(T).name());
}

template<typename T>
inline std::enable_if_t<std::is_lvalue_reference_v<T>, void> print_type()
{
    Utility::PrintMessage("%s &", typeid(T).name());
}

template<typename T>
inline std::enable_if_t<std::is_rvalue_reference_v<T>, void> print_type()
{
    Utility::PrintMessage("%s &&", typeid(T).name());
}


// Singleton
template<typename T>
class Singleton : public NonCopyable
{
public:
    static T* GetInstance() { return smSingleton; }

    template<typename ...Args>
    static T* GetOrCreateInstance(Args&& ...args)
    { 
        if (!smSingleton)
            return CreateInstance(std::forward<Args>(args)...);

        return smSingleton; 
    }

    static void RemoveInstance() { delete smSingleton; }
private:
    template<typename ...Args> static T* CreateInstance(Args&& ...args)
    {
        smSingleton = new T(std::forward<Args>(args)...);
        return smSingleton;
    }
private:
    static T* smSingleton;
};

template<typename T>
T* Singleton<T>::smSingleton = nullptr;


#define USE_SINGLETON template<typename> friend class Singleton
// Singleton


// const string strcat
inline char* _strcat(char* dest, const char* start)
{
    return std::strcat(dest, start);
}

inline wchar_t* _strcat(wchar_t* dest, const wchar_t* start)
{
    return std::wcscat(dest, start);
}

template<typename ...CharType>
inline constexpr size_t size_of_strs(CharType&& ...args)
{
    using _CharType = std::remove_const_t<std::remove_extent_t<deduce_first_t<std::remove_reference_t<CharType>...>>>;
    constexpr size_t strs_size = sizeof...(args);
    return ((ARRAYSIZE(args) + ...) - strs_size + 1) * sizeof(_CharType);
}

template<typename AllocType, typename ...CharType>
inline constexpr std::enable_if_t<!std::is_array_v<std::remove_reference_t<AllocType>>,
    std::add_pointer_t<std::remove_const_t<std::remove_extent_t<deduce_first_t<std::remove_reference_t<CharType>...>>>>>
concat_string_impl(AllocType&& salloca, CharType&& ...args)
{
    using _CharType = std::remove_const_t<std::remove_extent_t<deduce_first_t<std::remove_reference_t<CharType>...>>>;
    static_cast<_CharType*>(salloca)[0] = 0;
    return (_strcat(static_cast<_CharType*>(salloca), args), ...);
}

template<typename ...CharType>
inline constexpr auto concat_string_impl(CharType&& ...args)
{
    using _CharType = std::remove_const_t<std::remove_extent_t<deduce_first_t<std::remove_reference_t<CharType>...>>>;

    size_t all_strs_size = size_of_strs(std::forward<CharType>(args)...) / sizeof(_CharType);
    std::basic_string<_CharType> new_str(all_strs_size, 0);
    new_str.reserve();
    (_strcat(new_str.data(), args), ...);
    return new_str;
}

template<typename FirstType, typename ...CharType>
inline constexpr decltype(auto) concat_string(FirstType&& first, CharType&& ...args)
{
    using _CCharType = std::remove_extent_t<deduce_first_t<std::remove_reference_t<CharType>...>>;
    using _CharType = std::remove_const_t<_CCharType>;

    static_assert(std::is_same_v<_CharType, char> || std::is_same_v<_CharType, wchar_t>);
    static_assert((std::is_array_v<std::remove_reference_t<CharType>> && ...));
    static_assert((std::is_same_v<std::remove_extent_t<std::remove_reference_t<CharType>>, _CCharType> && ...));

    return concat_string_impl<FirstType, CharType...>(std::forward<FirstType>(first), std::forward<CharType>(args)...);
}

#define CONSTRA(...) concat_string(alloca(size_of_strs(__VA_ARGS__)), __VA_ARGS__)
#define CONSTRH(...) concat_string(__VA_ARGS__).c_str()
// const string strcat


// Collection
namespace CollectionHelper
{
    template<typename Arr>
    struct decay_arr
    {
        using T1 = std::remove_reference_t<Arr>;
        using ArrT = std::remove_extent_t<T1>;
        using type = std::conditional_t<std::is_array_v<T1>, std::add_pointer_t<ArrT>, Arr>;
    };
    template<typename Arr>
    using decay_arr_t = typename decay_arr<Arr>::type;

    template <typename First, typename Second>
    inline void CollectImpl2(First&& first, Second&& second)
    {
        //static_assert(false, "Not Impl Error");
        print_type<decltype(first)>();
        print_type<decltype(second)>();
        ASSERT(false, "Not Impl Error");
    }

    template <typename Tuple, std::size_t... Is>
    inline void CollectTuple2(Tuple&& tuple, std::index_sequence<Is...>)
    {
        (CollectImpl2<decay_arr_t<std::tuple_element_t<Is * 2, Tuple>>, decay_arr_t<std::tuple_element_t<Is * 2 + 1, Tuple>>>(
            std::forward<decay_arr_t<std::tuple_element_t<Is * 2, Tuple>>>(std::get<Is * 2>(tuple)),
            std::forward<decay_arr_t<std::tuple_element_t<Is * 2 + 1, Tuple>>>(std::get<Is * 2 + 1>(tuple))), ...);
    }

    template<typename ...Ts>
    inline void Collect2(Ts&& ...args)
    {
        CollectTuple2<std::tuple<Ts&&...>>(
            std::forward_as_tuple(std::forward<Ts>(args)...), std::make_index_sequence<sizeof...(Ts) / 2>{});
    }
};
// Collection

// main_thread_id
inline std::thread::id main_thread_id = std::this_thread::get_id();


namespace Graphics
{
    enum eDefaultTexture
    {
        kMagenta2D,  // Useful for indicating missing textures
        kBlackOpaque2D,
        kBlackTransparent2D,
        kWhiteOpaque2D,
        kWhiteTransparent2D,
        kDefaultNormalMap,
        kBlackCubeMap,

        kNumDefaultTextures
    };

    struct SharedSubResourceData
    {
        std::shared_ptr<const void> mpData;
        D3D12_SUBRESOURCE_DATA mSubResData;
    };

    template<enum D3D12_COMMAND_LIST_TYPE> inline constexpr int QUEUE_TYPE = 0;
    template<> inline constexpr int QUEUE_TYPE<D3D12_COMMAND_LIST_TYPE_DIRECT> = 0;
    template<> inline constexpr int QUEUE_TYPE<D3D12_COMMAND_LIST_TYPE_COMPUTE> = 1;
    template<> inline constexpr int QUEUE_TYPE<D3D12_COMMAND_LIST_TYPE_COPY> = 2;

    template<enum D3D12_COMMAND_LIST_TYPE> inline constexpr auto& QUEUE_TYPE_NAME = L"UnknownType";
    template<> inline constexpr auto& QUEUE_TYPE_NAME<D3D12_COMMAND_LIST_TYPE_DIRECT> = L"Graphics";
    template<> inline constexpr auto& QUEUE_TYPE_NAME<D3D12_COMMAND_LIST_TYPE_COMPUTE> = L"Compute";
    template<> inline constexpr auto& QUEUE_TYPE_NAME<D3D12_COMMAND_LIST_TYPE_COPY> = L"Copy";

    enum RENDER_TASK_TYPE
    {
        RENDER_TASK_TYPE_GRAPHICS = QUEUE_TYPE<D3D12_COMMAND_LIST_TYPE_DIRECT>,
        RENDER_TASK_TYPE_COMPUTE = QUEUE_TYPE<D3D12_COMMAND_LIST_TYPE_COMPUTE>,
        RENDER_TASK_TYPE_COPY = QUEUE_TYPE<D3D12_COMMAND_LIST_TYPE_COPY>,
        NUM_RENDER_TASK_TYPE,
    };

    inline D3D12_COMMAND_LIST_TYPE QUEUE_TYPE_RENDER[NUM_RENDER_TASK_TYPE] =
    {
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        D3D12_COMMAND_LIST_TYPE_COMPUTE,
        D3D12_COMMAND_LIST_TYPE_COPY,
    };

    inline constexpr RENDER_TASK_TYPE GetQueueType(const D3D12_COMMAND_LIST_TYPE& type)
    {
        switch (type)
        {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            return RENDER_TASK_TYPE_GRAPHICS;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            return RENDER_TASK_TYPE_COMPUTE;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            return RENDER_TASK_TYPE_COPY;
        default:
            throw std::runtime_error("Invalid type!!!");
        }
    }

    inline constexpr D3D12_COMMAND_LIST_TYPE GetQueueType(const RENDER_TASK_TYPE& type)
    {
        return QUEUE_TYPE_RENDER[type];
    }

    inline constexpr const wchar_t* GetQueueName(const D3D12_COMMAND_LIST_TYPE& type)
    {
        switch (type)
        {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            return QUEUE_TYPE_NAME<D3D12_COMMAND_LIST_TYPE_DIRECT>;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            return QUEUE_TYPE_NAME<D3D12_COMMAND_LIST_TYPE_COMPUTE>;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            return QUEUE_TYPE_NAME<D3D12_COMMAND_LIST_TYPE_COPY>;
        default:
            throw std::runtime_error("Invalid type!!!");
        }
    }
}

namespace std
{
    struct path_hash
    {
        size_t operator()(const std::filesystem::path& p) const
        {
            return std::filesystem::hash_value(p);
        }
    };
}
