#ifndef TEMPLATE_IMPL_H
#define TEMPLATE_IMPL_H

#include "jinja2cpp/value.h"
#include "jinja2cpp/template_env.h"
#include "internal_value.h"
#include "renderer.h"
#include "template_parser.h"
#include "value_visitors.h"

#include <boost/optional.hpp>
#include <nonstd/expected.hpp>
#include <string>


namespace jinja2
{

extern void SetupGlobals(InternalValueMap& globalParams);

class ITemplateImpl
{
public:
    virtual ~ITemplateImpl() = default;
};


template<typename U>
struct TemplateLoader;

template<>
struct TemplateLoader<char>
{
    static auto Load(const std::string& fileName, TemplateEnv* env)
    {
        return env->LoadTemplate(fileName);
    }
};

template<>
struct TemplateLoader<wchar_t>
{
    static auto Load(const std::string& fileName, TemplateEnv* env)
    {
        return env->LoadTemplateW(fileName);
    }
};

template<typename CharT>
class GenericStreamWriter : public OutStream::StreamWriter
{
public:
    explicit GenericStreamWriter(std::basic_string<CharT>& os)
        : m_os(os)
    {}

    // StreamWriter interface
    void WriteBuffer(const void* ptr, size_t length) override
    {
        m_os.append(reinterpret_cast<const CharT*>(ptr), length);
    }
    void WriteValue(const InternalValue& val) override
    {
        Apply<visitors::ValueRenderer<CharT>>(val, m_os);
    }

private:
    std::basic_string<CharT>& m_os;
};

template<typename CharT>
class StringStreamWriter : public OutStream::StreamWriter
{
public:
    explicit StringStreamWriter(std::basic_string<CharT>* targetStr)
        : m_targetStr(targetStr)
    {}

    // StreamWriter interface
    void WriteBuffer(const void* ptr, size_t length) override
    {
        m_targetStr->append(reinterpret_cast<const CharT*>(ptr), length);
        // m_os.write(reinterpret_cast<const CharT*>(ptr), length);
    }
    void WriteValue(const InternalValue& val) override
    {
        Apply<visitors::ValueRenderer<CharT>>(val, *m_targetStr);
    }

private:
    std::basic_string<CharT>* m_targetStr;
};

template<typename ErrorTpl1, typename ErrorTpl2>
struct ErrorConverter;

template<typename CharT1, typename CharT2>
struct ErrorConverter<ErrorInfoTpl<CharT1>, ErrorInfoTpl<CharT2>>
{
    static ErrorInfoTpl<CharT1> Convert(const ErrorInfoTpl<CharT2>& srcError)
    {
        typename ErrorInfoTpl<CharT1>::Data errorData;
        errorData.code = srcError.GetCode();
        errorData.srcLoc = srcError.GetErrorLocation();
        errorData.locationDescr = ConvertString<std::basic_string<CharT1>>(srcError.GetLocationDescr());
        errorData.extraParams = srcError.GetExtraParams();

        return ErrorInfoTpl<CharT1>(errorData);
    }
};

template<typename CharT>
struct ErrorConverter<ErrorInfoTpl<CharT>, ErrorInfoTpl<CharT>>
{
    static const ErrorInfoTpl<CharT>& Convert(const ErrorInfoTpl<CharT>& srcError)
    {
        return srcError;
    }
};
        
template<typename CharT>
class TemplateImpl : public ITemplateImpl
{
public:
    using ThisType = TemplateImpl<CharT>;

    explicit TemplateImpl(TemplateEnv* env)
        : m_env(env)
    {
        if (env)
            m_settings = env->GetSettings();
    }

    auto GetRenderer() const {return m_renderer;}
    auto GetTemplateName() const {};

    boost::optional<ErrorInfoTpl<CharT>> Load(std::basic_string<CharT> tpl, std::string tplName)
    {
        m_template = std::move(tpl);
        m_templateName = tplName.empty() ? std::string("noname.j2tpl") : std::move(tplName);
        TemplateParser<CharT> parser(&m_template, m_settings, m_env, m_templateName);

        auto parseResult = parser.Parse();
        if (!parseResult)
            return parseResult.error()[0];

        m_renderer = *parseResult;
        return boost::optional<ErrorInfoTpl<CharT>>();
    }

    boost::optional<ErrorInfoTpl<CharT>> Render(std::basic_string<CharT>& os, const ValuesMap& params)
    {
        boost::optional<ErrorInfoTpl<CharT>> normalResult;

        if (!m_renderer)
        {
            typename ErrorInfoTpl<CharT>::Data errorData;
            errorData.code = ErrorCode::TemplateNotParsed;
            errorData.srcLoc.col = 1;
            errorData.srcLoc.line = 1;
            errorData.srcLoc.fileName = "<unknown file>";

            return ErrorInfoTpl<CharT>(errorData);
        }

        try
        {
            InternalValueMap extParams;
            InternalValueMap intParams;

            auto convertFn = [&intParams](auto& params) {
                for (auto& ip : params)
                {
                    auto valRef = &ip.second.data();
                    auto newParam = visit(visitors::InputValueConvertor(false, true), *valRef);
                    if (!newParam)
                        intParams[ip.first] = ValueRef(static_cast<const Value&>(*valRef));
                    else
                        intParams[ip.first] = newParam.get();
                }
            };

            if (m_env)
            {
                m_env->ApplyGlobals(convertFn);
                std::swap(extParams, intParams);
            }

            convertFn(params);
            SetupGlobals(extParams);

            RendererCallback callback(this);
            RenderContext context(intParams, extParams, &callback);
            InitRenderContext(context);
            OutStream outStream([writer = GenericStreamWriter<CharT>(os)]() mutable -> OutStream::StreamWriter* {return &writer;});
            m_renderer->Render(outStream, context);
        }
        catch (const ErrorInfoTpl<char>& error)
        {
            return ErrorConverter<ErrorInfoTpl<CharT>, ErrorInfoTpl<char>>::Convert(error);
        }
        catch (const ErrorInfoTpl<wchar_t>& error)
        {
            return ErrorConverter<ErrorInfoTpl<CharT>, ErrorInfoTpl<wchar_t>>::Convert(error);
        }
        catch (const std::exception& ex)
        {
            typename ErrorInfoTpl<CharT>::Data errorData;
            errorData.code = ErrorCode::UnexpectedException;
            errorData.srcLoc.col = 1;
            errorData.srcLoc.line = 1;
            errorData.srcLoc.fileName = m_templateName;
            errorData.extraParams.push_back(Value(std::string(ex.what())));

            return ErrorInfoTpl<CharT>(errorData);
        }

        return normalResult;
    }

    InternalValueMap& InitRenderContext(RenderContext& context)
    {
        auto& curScope = context.GetCurrentScope();
        return curScope;
    }

    using TplLoadResultType = nonstd::variant<EmptyValue,
            nonstd::expected<std::shared_ptr<TemplateImpl<char>>, ErrorInfo>,
            nonstd::expected<std::shared_ptr<TemplateImpl<wchar_t>>, ErrorInfoW>>;

    using TplOrError = nonstd::expected<std::shared_ptr<TemplateImpl<CharT>>, ErrorInfoTpl<CharT>>;

    TplLoadResultType LoadTemplate(const std::string& fileName)
    {
        if (!m_env)
            return TplLoadResultType(EmptyValue());

        auto tplWrapper = TemplateLoader<CharT>::Load(fileName, m_env);
        if (!tplWrapper)
            return TplLoadResultType(TplOrError(tplWrapper.get_unexpected()));

        return TplLoadResultType(TplOrError(std::static_pointer_cast<ThisType>(tplWrapper.value().m_impl)));
    }

    TplLoadResultType LoadTemplate(const InternalValue& fileName)
    {
        auto name = GetAsSameString(std::string(), fileName);
        if (!name)
        {
            typename ErrorInfoTpl<CharT>::Data errorData;
            errorData.code = ErrorCode::InvalidTemplateName;
            errorData.srcLoc.col = 1;
            errorData.srcLoc.line = 1;
            errorData.srcLoc.fileName = m_templateName;
            errorData.extraParams.push_back(IntValue2Value(fileName));
            return TplOrError(nonstd::make_unexpected(ErrorInfoTpl<CharT>(errorData)));
        }

        return LoadTemplate(name.value());
    }

    void ThrowRuntimeError(ErrorCode code, ValuesList extraParams)
    {
        typename ErrorInfoTpl<CharT>::Data errorData;
        errorData.code = code;
        errorData.srcLoc.col = 1;
        errorData.srcLoc.line = 1;
        errorData.srcLoc.fileName = m_templateName;
        errorData.extraParams = std::move(extraParams);

        throw ErrorInfoTpl<CharT>(std::move(errorData));
    }

    class RendererCallback : public IRendererCallback
    {
    public:
        explicit RendererCallback(ThisType* host)
            : m_host(host)
        {}

        TargetString GetAsTargetString(const InternalValue& val) override
        {
            std::basic_string<CharT> os;
            Apply<visitors::ValueRenderer<CharT>>(val, os);
            return TargetString(std::move(os));
        }

        OutStream GetStreamOnString(TargetString& str) override
        {
            using string_t = std::basic_string<CharT>;
            str = string_t();
            return OutStream([writer = StringStreamWriter<CharT>(&str.get<string_t>())]() mutable -> OutStream::StreamWriter* {return &writer;});
        }

        nonstd::variant<EmptyValue,
            nonstd::expected<std::shared_ptr<TemplateImpl<char>>, ErrorInfo>,
            nonstd::expected<std::shared_ptr<TemplateImpl<wchar_t>>, ErrorInfoW>> LoadTemplate(const std::string& fileName) const override
        {
            return m_host->LoadTemplate(fileName);
        }

        nonstd::variant<EmptyValue,
                nonstd::expected<std::shared_ptr<TemplateImpl<char>>, ErrorInfo>,
                nonstd::expected<std::shared_ptr<TemplateImpl<wchar_t>>, ErrorInfoW>> LoadTemplate(const InternalValue& fileName) const override
        {
            return m_host->LoadTemplate(fileName);
        }

        void ThrowRuntimeError(ErrorCode code, ValuesList extraParams) override
        {
            m_host->ThrowRuntimeError(code, std::move(extraParams));
        }

    private:
        ThisType* m_host;
    };

private:
    TemplateEnv* m_env;
    Settings m_settings;
    std::basic_string<CharT> m_template;
    std::string m_templateName;
    RendererPtr m_renderer;
};

} // jinja2

#endif // TEMPLATE_IMPL_H
