#ifndef JINJA2_VALUE_H
#define JINJA2_VALUE_H

#include "generic_list.h"
#include "value_ptr.hpp"

#include <nonstd/variant.hpp>
#include <nonstd/optional.hpp>
#include <nonstd/string_view.hpp>

#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include <type_traits>

namespace jinja2
{
//! Empty value container
struct EmptyValue
{
    template<typename T>
    operator T() const {return T{};}
};
class Value;

/*!
 * \brief Interface to the generic dictionary type which maps string to some value
 */
struct MapItemAccessor
{
    //! Destructor
    virtual ~MapItemAccessor() = default;

    //! Method is called to obtain number of items in the dictionary. Maximum possible size_t value means non-calculable size
    virtual size_t GetSize() const = 0;

    /*!
     * \brief Method is called to check presence of the item in the dictionary
     *
     * @param name Name of the item
     *
     * @return true if item is present and false otherwise.
     */
    virtual bool HasValue(const std::string& name) const = 0;
    /*!
     * \brief Method is called for retrieving the value by specified name
     *
     * @param name Name of the value to retrieve
     *
     * @return Requestd value or empty \ref Value if item is absent
     */
    virtual Value GetValueByName(const std::string& name) const = 0;
    /*!
     * \brief Method is called for retrieving collection of keys in the dictionary
     *
     * @return Collection of keys if any. Ordering of keys is unspecified.
     */
    virtual std::vector<std::string> GetKeys() const = 0;
};

/*!
 * \brief Helper class for accessing maps specified by the \ref MapItemAccessor interface
 *
 * In the \ref Value type can be stored either ValuesMap instance or GenericMap instance. ValuesMap is a simple
 * dictionary object based on std::unordered_map. Rather than GenericMap is a more robust object which can provide
 * access to the different types of dictionary entities. GenericMap takes the \ref MapItemAccessor interface instance
 * and uses it to access particular items in the dictionaries.
 */
class GenericMap
{
public:
    //! Default constructor
    GenericMap() = default;

    /*!
     * \brief Initializing constructor
     *
     * The only one way to get valid non-empty GeneridMap is to construct it with the specified \ref MapItemAccessor
     * implementation provider. This provider is a functional object which returns pointer to the interface instance.
     *
     * @param accessor Functional object which returns pointer to the \ref MapItemAccessor interface
     */
    explicit GenericMap(std::function<const MapItemAccessor* ()> accessor)
        : m_accessor(std::move(accessor))
    {
    }

    /*!
     * \brief Check the presence the specific item in the dictionary
     *
     * @param name Name of the the item
     *
     * @return true of item is present and false otherwise
     */
    bool HasValue(const std::string& name) const
    {
        return m_accessor ? m_accessor()->HasValue(name) : false;
    }

    /*!
     * \brief Get specific item from the dictionary
     *
     * @param name Name of the item to get
     *
     * @return Value of the item or empty \ref Value if no item
     */
    Value GetValueByName(const std::string& name) const;
    /*!
     * \brief Get size of the dictionary
     *
     * @return Size of the dictionary
     */
    size_t GetSize() const
    {
        return m_accessor ? m_accessor()->GetSize() : 0;
    }
    /*!
     * \brief  Get collection of keys from the dictionary
     *
     * @return Collection of the keys or empty collection if no keys
     */
    auto GetKeys() const
    {
        return m_accessor ? m_accessor()->GetKeys() : std::vector<std::string>();
    }
    /*!
     * \brief Get the underlying access interface to the dictionary
     *
     * @return Pointer to the underlying interface or nullptr if no
     */
    auto GetAccessor() const
    {
        return m_accessor();
    }

private:
    std::function<const MapItemAccessor* ()> m_accessor;
};

using ValuesList = std::vector<Value>;
using ValuesMap = std::unordered_map<std::string, Value>;
struct UserCallableArgs;
struct ParamInfo;
struct UserCallable;

template<typename T>
using RecWrapper = nonstd::value_ptr<T>;

/*!
 * \brief Generic value class
 *
 * Variant-based class which is used for passing values to and from Jinja2C++ template engine. This class store the
 * following types of values:
 *
 *  - EmptyValue. In this case instance of this class threated as 'empty'
 *  - Boolean value.
 *  - String value.
 *  - Wide string value
 *  - String view value (nonstd::string_view)
 *  - Wide string view value (nonstd::wstring_view)
 *  - integer (int64_t) value
 *  - floating point (double) value
 *  - Simple list of other values (\ref ValuesList)
 *  - Simple map of other values (\ref ValuesMap)
 *  - Generic list of other values (\ref GenericList)
 *  - Generic map of other values (\ref GenericMap)
 *  - User-defined callable (\ref UserCallable)
 *
 *  Exact value can be accessed via nonstd::visit method applied to the result of the Value::data() call or any of
 *  asXXX method (ex. \ref Value::asString). In case of string retrieval it's better to use \ref AsString or \ref
 *  AsWString functions. Thay hide all nececcary transformations between various types of strings (or string views).
 */
class Value
{
public:
    using ValueData = nonstd::variant<
        EmptyValue,
        bool,
        std::string,
        std::wstring,
        nonstd::string_view,
        nonstd::wstring_view,
        int64_t,
        double,
        RecWrapper<ValuesList>,
        RecWrapper<ValuesMap>,
        GenericList,
        GenericMap,
        RecWrapper<UserCallable>
     >;

    template<typename T, typename ... L>
    struct AnyOf : public std::false_type {};

    template<typename T, typename H, typename ... L>
    struct AnyOf<T, H, L...> : public std::integral_constant<bool, std::is_same<std::decay_t<T>, H>::value || AnyOf<T, L...>::value> {};

    //! Default constructor
    Value();
    //! Copy constructor
    Value(const Value& val);
    //! Move constructor
    Value(Value&& val) noexcept;
    //! Desctructor
    ~Value();

    //! Assignment operator
    Value& operator =(const Value&);
    //! Move assignment operator
    Value& operator =(Value&&) noexcept;
    /*!
     * \brief Generic initializing constructor
     *
     * Creates \ref Value from the arbitrary type which is compatible with types listed in \ref Value::ValueData
     *
     * @tparam T  Type of value to create \ref Value instance from
     * @param val Value which should be used to initialize \ref Value instance
     */
    template<typename T>
    Value(T&& val, typename std::enable_if<!AnyOf<T, Value, ValuesList, ValuesMap, UserCallable>::value>::type* = nullptr)
        : m_data(std::forward<T>(val))
    {
    }
    /*!
     * \brief Initializing constructor from pointer to the null-terminated narrow string
     *
     * @param val Null-terminated string which should be used to initialize \ref Value instance
     */
    Value(const char* val)
        : m_data(std::string(val))
    {
    }
    /*!
     * \brief Initializing constructor from pointer to the null-terminated wide string
     *
     * @param val Null-terminated string which should be used to initialize \ref Value instance
     */
    Value(const wchar_t* val)
        : m_data(std::wstring(val))
    {
    }
    /*!
     * \brief Initializing constructor from the narrow string literal
     *
     * @param val String literal which should be used to initialize \ref Value instance
     */
    template<size_t N>
    Value(char (&val)[N])
        : m_data(std::string(val))
    {
    }
    /*!
     * \brief Initializing constructor from the wide string literal
     *
     * @param val String literal which should be used to initialize \ref Value instance
     */
    template<size_t N>
    Value(wchar_t (&val)[N])
        : m_data(std::wstring(val))
    {
    }
    /*!
     * \brief Initializing constructor from the int value
     *
     * @param val Integer value which should be used to initialize \ref Value instance
     */
    Value(int val)
        : m_data(static_cast<int64_t>(val))
    {
    }
    /*!
     * \brief Initializing constructor from the \ref ValuesList
     *
     * @param list List of values which should be used to initialize \ref Value instance
     */
    Value(const ValuesList& list)
        : m_data(RecWrapper<ValuesList>(list))
    {
    }
    /*!
     * \brief Initializing constructor from the \ref ValuesMap
     *
     * @param map Map of values which should be used to initialize \ref Value instance
     */
    Value(const ValuesMap& map)
        : m_data(RecWrapper<ValuesMap>(map))
    {
    }
    /*!
     * \brief Initializing constructor from the \ref UserCallable
     *
     * @param callable UserCallable which should be used to initialize \ref Value instance
     */
    Value(const UserCallable& callable);
    /*!
     * \brief Initializing move constructor from the \ref ValuesList
     *
     * @param list List of values which should be used to initialize \ref Value instance
     */
    Value(ValuesList&& list) noexcept
        : m_data(RecWrapper<ValuesList>(std::move(list)))
    {
    }
    /*!
     * \brief Initializing move constructor from the \ref ValuesMap
     *
     * @param map Map of values which should be used to initialize \ref Value instance
     */
    Value(ValuesMap&& map) noexcept
        : m_data(RecWrapper<ValuesMap>(std::move(map)))
    {
    }
    /*!
     * \brief Initializing move constructor from the \ref UserCallable
     *
     * @param callable UserCallable which should be used to initialize \ref Value instance
     */
    Value(UserCallable&& callable);

    /*!
     * \brief Get the non-mutable stored data object
     *
     * Returns the stored data object in order to get the typed value from it. For instance:
     * ```c++
     *  inline std::string AsString(const jinja2::Value& val)
     *  {
     *      return nonstd::visit(StringGetter(), val.data());
     *  }
     *  ```
     *
     * @return Non-mutable stored data object
     */
    const ValueData& data() const {return m_data;}
    /*!
     * \brief Get the mutable stored data object
     *
     * Returns the stored data object in order to get the typed value from it. For instance:
     * ```c++
     *  inline std::string AsString(Value& val)
     *  {
     *      return nonstd::visit(StringGetter(), val.data());
     *  }
     *  ```
     *
     * @return Mutable stored data object
     */
    ValueData& data() {return m_data;}

    //! Test Value for containing std::string object
    bool isString() const
    {
        return nonstd::get_if<std::string>(&m_data) != nullptr;
    }
    /*!
     * \brief Returns mutable containing std::string object
     *
     * Returns containing std::string object. Appropriate exception is thrown in case non-string containing value
     *
     * @return Mutable containing std::string object
     */
    auto& asString()
    {
        return nonstd::get<std::string>(m_data);
    }
    /*!
     * \brief Returns non-mutable containing std::string object
     *
     * Returns containing std::string object. Appropriate exception is thrown in case of non-string containing value
     *
     * @return Non-mutable containing std::string object
     */
    auto& asString() const
    {
        return nonstd::get<std::string>(m_data);
    }

    //! Test Value for containing std::wstring object
    bool isWString() const
    {
        return nonstd::get_if<std::wstring>(&m_data) != nullptr;
    }
    /*!
     * \brief Returns mutable containing std::wstring object
     *
     * Returns containing std::wstring object. Appropriate exception is thrown in case of non-wstring containing value
     *
     * @return Mutable containing std::wstring object
     */
    auto& asWString()
    {
        return nonstd::get<std::wstring>(m_data);
    }
    /*!
     * \brief Returns non-mutable containing std::wstring object
     *
     * Returns containing std::wstring object. Appropriate exception is thrown in case of non-wstring containing value
     *
     * @return Non-mutable containing std::wstring object
     */
    auto& asWString() const
    {
        return nonstd::get<std::wstring>(m_data);
    }

    //! Test Value for containing jinja2::ValuesList object
    bool isList() const
    {
        return nonstd::get_if<RecWrapper<ValuesList>>(&m_data) != nullptr || nonstd::get_if<GenericList>(&m_data) != nullptr;
    }
    /*!
     * \brief Returns mutable containing jinja2::ValuesList object
     *
     * Returns containing jinja2::ValuesList object. Appropriate exception is thrown in case of non-Valueslist containing value
     *
     * @return Mutable containing jinja2::ValuesList object
     */
    auto& asList()
    {
        return *nonstd::get<RecWrapper<ValuesList>>(m_data).get();
    }
    /*!
     * \brief Returns non-mutable containing jinja2::ValuesList object
     *
     * Returns containing jinja2::ValuesList object. Appropriate exception is thrown in case of non-Valueslist containing value
     *
     * @return Non-mutable containing jinja2::ValuesList object
     */
    auto& asList() const
    {
        return *nonstd::get<RecWrapper<ValuesList>>(m_data).get();
    }
    //! Test Value for containing jinja2::ValuesMap object
    bool isMap() const
    {
        return nonstd::get_if<RecWrapper<ValuesMap>>(&m_data) != nullptr || nonstd::get_if<GenericMap>(&m_data) != nullptr;
    }
    /*!
     * \brief Returns mutable containing jinja2::ValuesMap object
     *
     * Returns containing jinja2::ValuesMap object. Appropriate exception is thrown in case of non-ValuesMap containing value
     *
     * @return Mutable containing jinja2::ValuesMap object
     */
    auto& asMap()
    {
        return *nonstd::get<RecWrapper<ValuesMap>>(m_data).get();
    }
    /*!
     * \brief Returns non-mutable containing jinja2::ValuesMap object
     *
     * Returns containing jinja2::ValuesMap object. Appropriate exception is thrown in case of non-ValuesMap containing value
     *
     * @return Non-mutable containing jinja2::ValuesMap object
     */
    auto& asMap() const
    {
        return *nonstd::get<RecWrapper<ValuesMap>>(m_data).get();
    }
    //! Test Value for emptyness
    bool isEmpty() const
    {
        return nonstd::get_if<EmptyValue>(&m_data) != nullptr;
    }

private:
    ValueData m_data;
};


/*!
 * \brief Information about user-callable parameters passed from Jinja2 call context
 *
 * This structure prepared by the Jinja2C++ engine and filled by information about call parameters gathered from the
 * call context. See documentation for \ref UserCallable for detailed information
 *
 */
struct UserCallableParams
{
    //! Values of parameters mapped according to \ref UserCallable::argsInfo
    ValuesMap args;
    //! Values of extra positional args got from the call expression
    Value extraPosArgs;
    //! Values of extra named args got from the call expression
    Value extraKwArgs;
    //! Context object which provides access to the current variables set of the template
    Value context;
    bool paramsParsed = false;

    Value operator[](const std::string& paramName) const
    {
        auto p = args.find(paramName);
        if (p == args.end())
            return Value();

        return p->second;
    }
};

/*!
 * \brief Information about one argument of the user-defined callable
 *
 * This structure is used as a description of the user-callable argument. Information from this structure is used
 * by the Jinja2C++ engine to map actual call parameters to the expected ones by the user-defined callable.
 */
struct ArgInfo
{
    //! Name of the argument
    std::string paramName;
    //! Mandatory flag
    bool isMandatory;
    //! Default value for the argument
    Value defValue;

    ArgInfo(std::string name, bool isMandat = false, Value defVal = Value())
        : paramName(std::move(name))
        , isMandatory(isMandat)
        , defValue(std::move(defVal)) {}
};

template<typename T>
struct ArgInfoT : public ArgInfo
{
    using type = T;

    using ArgInfo::ArgInfo;
    ArgInfoT(const ArgInfo& info)
        : ArgInfo(info)
    {
    }
    ArgInfoT(ArgInfo&& info) noexcept
        : ArgInfo(std::move(info))
    {
    }
};

/*!
 * \brief User-callable descriptor
 *
 * This descriptor is used for description of the user-defined callables passed to the Jinja2C++ engine. Information
 * from this descriptor is used by the engine to properly parse and prepare of the call parameters and pass it to the
 * user-callable. For instance, such kind of user-defined callable passed as a parameter:
 * ```c++
 *  jinja2::UserCallable uc;
 *  uc.callable = [](auto& params)->jinja2::Value {
 *      auto str1 = params["str1"];
 *      auto str2 = params["str2"];
 *
 *      if (str1.isString())
 *          return str1.asString() + " " + str2.asString();
 *
 *      return str1.asWString() + L" " + str2.asWString();
 *  };
 *  uc.argsInfo = {{"str1", true}, {"str2", true}};
 *  params["test"] = std::move(uc);
 * ```
 * This declaration defines user-defined callable which takes two named parameters: `str1` and `str2`. Further, it's
 * possible to call this user-defined callable from the Jinja2 template this way:
 * ```jinja2
 * {{ test('Hello', 'World!') }}
 * ```
 * or:
 * ```
 * {{ test(str2='World!', str1='Hello') }}
 * ```
 * Jinja2C++ engine maps actual call parameters according the information from \ref UserCallable::argsInfo field and
 * pass them as a \ref UserCallableParams structure. Every named param (explicitly defined in the call or it's default value)
 * passed throught \ref UserCallableParams::args field. Every extra positional param mentoined in call passed as \ref
 * UserCallableParams::extraPosArgs. Every extra named param mentoined in call passed as \ref
 * UserCallableParams::extraKwArgs.
 *
 * If any of argument, marked as `mandatory` in the \ref UserCallable::argsInfo field is missed in the point of the
 * user-defined call the call is failed.
 */
struct UserCallable
{
    //! Functional object which is actually handle the call
    std::function<Value (const UserCallableParams&)> callable;
    //! Information about arguments of the user-defined callable
    std::vector<ArgInfo> argsInfo;
};

inline Value::Value(const UserCallable& callable)
    : m_data(RecWrapper<UserCallable>(callable))
{
}

inline Value::Value(UserCallable&& callable)
    : m_data(RecWrapper<UserCallable>(std::move(callable)))
{
}

inline Value GenericMap::GetValueByName(const std::string& name) const
{
    return m_accessor ? m_accessor()->GetValueByName(name) : Value();
}

inline Value::Value() = default;
inline Value::Value(const Value& val) = default;
inline Value::Value(Value&& val) noexcept
    : m_data(std::move(val.m_data))
{
}
inline Value::~Value() = default;
inline Value& Value::operator =(const Value&) = default;
inline Value& Value::operator =(Value&& val) noexcept
{
    if (this == &val)
        return *this;

    m_data.swap(val.m_data);
    return *this;
}


} // jinja2

#endif // JINJA2_VALUE_H
