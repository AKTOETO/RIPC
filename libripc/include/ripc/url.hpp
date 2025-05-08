#ifndef RIPC_URL_HPP
#define RIPC_URL_HPP

#include <string>
#include <vector>
#include <variant>
#include <stdexcept>

namespace ripc
{

    template <typename DynTok>
    class IUrl
    {
    protected:
        // список возможных токенов в url
        using Token = std::variant<std::string, DynTok>;

        // список токенов из url
        std::vector<Token> m_pattern;
        // url в виде строки
        std::string m_url;

        // разбиение паттерна на части
        void subdivide(const std::string &str);
        virtual DynTok processDynTok(const std::string &tok) = 0;

    public:
        IUrl(const std::string &pattern) : m_url(pattern) { subdivide(m_url); }
        IUrl(const char *pattern) : m_url(pattern) { subdivide(m_url); }

        // запрет копирования
        IUrl(const IUrl &) = delete;
        IUrl &operator=(const IUrl &) = delete;

        // разрешаем перемещение
        IUrl(IUrl &&other) noexcept = default;
        IUrl &operator=(IUrl &&other) noexcept = default;

        // получить список токенов
        const std::vector<Token> &getTokens() const { return m_pattern; }

        // получить URL строкой
        const std::string &getUrl() const { return m_url; }

        // сравнение двух паттернов
        bool operator<(const IUrl &pat) { return m_pattern < pat.m_pattern; }

        
        // вывод в поток
        friend std::ostream &operator<<(std::ostream &out, const IUrl &url)
        {
            out << url.m_url;
            return out;
        }
    };
    
    // токенизированный url
    namespace DynTok
    {
        using Url = std::variant<std::string, int>;
        enum class UrlPattern
        {
            NONE,
            STRING,
            INT
        };
    };

    class UrlPattern;
    class Url : public IUrl<DynTok::Url>
    {
    private:
        virtual DynTok::Url processDynTok(const std::string &tok) override;

    public:
        Url(const std::string &pattern);
        Url(const char *pattern);

        friend bool operator==(const Url &url, const UrlPattern &pattern);
        friend bool operator==(const UrlPattern &pattern, const Url &url);
    };

    // шаблон для проверки url
    class UrlPattern : public IUrl<DynTok::UrlPattern>
    {
    private:
        virtual DynTok::UrlPattern processDynTok(const std::string &tok) override;

    public:
        UrlPattern(const std::string &pattern);
        UrlPattern(const char *pattern);

        friend bool operator==(const Url &url, const UrlPattern &pattern);
        friend bool operator==(const UrlPattern &pattern, const Url &url);
        friend bool operator<(const UrlPattern &p1, const UrlPattern &p2);
    };

    // --- IURL ---
    // Реализация шаблонного метода для итерфейсного класса
    template <typename DynTok>
    void IUrl<DynTok>::subdivide(const std::string &str)
    {
        // текущий токен
        std::string current;
        // проверка на символы <>
        bool in_brackets = false;
        // ищем символ '/' и делим по нему строку на токены
        for (char ch : str)
        {
            if (ch == '<')
            {
                if (in_brackets)
                {
                    // два открывающих <
                    throw std::invalid_argument("UrlPattern::subdivide: two '<' in a row");
                }
                in_brackets = true;

                if (!current.empty())
                {
                    m_pattern.emplace_back(current);
                    current.clear();
                }
            }
            else if (ch == '>')
            {
                if (!in_brackets)
                {
                    // не было открывающей скобки
                    throw std::invalid_argument("UrlPattern::subdivide: there was no '<' before '>'");
                }

                // Пытаемся определить тип содержимого
                if (current.empty())
                {
                    throw std::invalid_argument("UrlPattern::subdivide: Empty <>");
                }

                // обработка динамического токена
                m_pattern.emplace_back(processDynTok(current));
                current.clear();
            }
            else if (ch == '/')
            {
                if (in_brackets)
                {
                    current += ch;
                }
                else
                {
                    if (!current.empty())
                    {
                        m_pattern.emplace_back(current);
                        current.clear();
                    }
                }
            }
            else
            {
                current += ch;
            }
        }
        // Обработка оставшихся данных
        if (in_brackets)
        {
            throw std::invalid_argument("Unclosed <");
        }

        if (!current.empty())
        {
            m_pattern.emplace_back(current);
        }
    }

} // namespace ripc

#endif // !RIPC_URL_HPP