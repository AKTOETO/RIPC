#ifndef RIPC_URL_HPP
#define RIPC_URL_HPP

#include <string>
#include <vector>
#include <variant>
#include <stdexcept>

namespace ripc
{

    class Url;
    class UrlPattern;

    namespace TokenType
    {
        // типы для обычного url
        namespace Url
        {
            using Type = std::variant<std::string, int>;
        };

        // типы для шаблонного url
        namespace UrlPattern
        {
            using Static = std::variant<std::string, int>;

            enum class Dynamic
            {
                STRING,
                INT
            };

            using Type = std::variant<Static, Dynamic>;
        };
    }

    template <typename TokenType>
    class IUrl
    {
    protected:
        // список токенов из url
        std::vector<TokenType> m_pattern;
        // url в виде строки
        std::string m_url;

        // разбиение паттерна на части
        void subdivide();
        virtual TokenType processToken(const std::string &tok) = 0;

    public:
        IUrl(const std::string &pattern) : m_url(pattern) {}
        IUrl(const char *pattern) : m_url(pattern) {}
        virtual ~IUrl() {};

        // запрет копирования
        IUrl(const IUrl &) = delete;
        IUrl &operator=(const IUrl &) = delete;

        // разрешаем перемещение
        IUrl(IUrl &&other) noexcept = default;
        IUrl &operator=(IUrl &&other) noexcept = default;

        // получить список токенов
        const std::vector<TokenType> &getTokens() const { return m_pattern; }

        // получить URL строкой
        const std::string &getUrl() const { return m_url; }

        // сравнение двух паттернов
        friend bool operator<(const IUrl &pat, const IUrl &pat2) { return pat.m_pattern < pat2.m_pattern; }

        // вывод в поток
        friend std::ostream &operator<<(std::ostream &out, const IUrl &url)
        {
            out << url.m_url;
            return out;
        }
    };

    // Класс обычного Url
    class Url : public IUrl<TokenType::Url::Type>
    {
    private:
        // обработка токена
        virtual TokenType::Url::Type processToken(const std::string &tok) override;

    public:
        Url(const std::string &pattern);
        Url(const char *pattern);
        ~Url() {};

        // разрешаем перемещение
        Url(Url &&other) noexcept = default;
        Url &operator=(Url &&other) noexcept = default;

        // операции сравнения
        friend bool operator==(const Url &url, const UrlPattern &pattern);
        friend bool operator==(const UrlPattern &pattern, const Url &url);
    };

    // Класс шаблонного Url
    class UrlPattern : public IUrl<TokenType::UrlPattern::Type>
    {
    private:
        // обработка токена
        virtual TokenType::UrlPattern::Type processToken(const std::string &tok) override;

    public:
        UrlPattern(const std::string &pattern);
        UrlPattern(const char *pattern);
        ~UrlPattern() {};

        // разрешаем перемещение
        UrlPattern(UrlPattern &&other) noexcept = default;
        UrlPattern &operator=(UrlPattern &&other) noexcept = default;

        // операции сравнения
        friend bool operator==(const Url &url, const UrlPattern &pattern);
        friend bool operator==(const UrlPattern &pattern, const Url &url);
    };

    // --- IURL ---
    // Реализация шаблонного метода для итерфейсного класса
    template <typename TokenType>
    void IUrl<TokenType>::subdivide()
    {
        // текущий токен
        std::string current;

        // проходимся по строке и разбиваем ее на токены
        for (char ch : m_url)
        {
            if (ch == '/')
            {
                if (!current.empty())
                {
                    // вызываем обработчик токена и добавляем его
                    m_pattern.emplace_back(processToken(current));
                    current.clear();
                }
            }
            else
            {
                current += ch;
            }
        }

        // если токен еще остался
        if (!current.empty())
        {
            m_pattern.emplace_back(processToken(current));
        }
    }
} // namespace ripc

#endif // !RIPC_URL_HPP