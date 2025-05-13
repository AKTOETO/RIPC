#include "ripc/logger.hpp"
#include "ripc/url.hpp"
#include <stdexcept>
#include <algorithm>
#include <iostream>

namespace ripc
{
    //--- URL ---

    Url::Url(std::string_view pattern)
        : IUrl<TokenType::Url::Type>(pattern)
    {
        subdivide();
    }

    Url::Url(const std::string &url)
        : IUrl<TokenType::Url::Type>(url)
    {
        subdivide();
    }

    Url::Url(const char *url)
        : IUrl<TokenType::Url::Type>(url)
    {
        subdivide();
    }

    TokenType::Url::Type Url::processToken(const std::string &tok)
    {
        // возврат int, если там число
        if (std::all_of(tok.begin(), tok.end(), ::isdigit))
        {
            return std::stoi(tok);
        }

        // возврат строки
        return tok;
    }

    // --- Pattern ---

    UrlPattern::UrlPattern(std::string_view pattern)
        : IUrl<TokenType::UrlPattern::Type>(pattern)
    {
        subdivide();
    }

    UrlPattern::UrlPattern(const std::string &pattern)
        : IUrl<TokenType::UrlPattern::Type>(pattern)
    {
        subdivide();
    }

    UrlPattern::UrlPattern(const char *url)
        : IUrl<TokenType::UrlPattern::Type>(url)
    {
        subdivide();
    }

    TokenType::UrlPattern::Type UrlPattern::processToken(const std::string &tok)
    {
        // Порядок определения типа токена
        // 1. Нужно понять: статический токен или динамический
        // 2. Если статический, то какой тип: std::string или int
        // 3. Если динамический, то какой тип: Dynamic::STRING или Dynamic::INT

        // если первый и последний символы токена равны '<' и '>' соответсвенно,
        // то это динамический токен
        if (tok[0] == '<' && tok[tok.size() - 1] == '>')
        {
            if (tok == "<string>")
            {
                return TokenType::UrlPattern::Dynamic::STRING;
            }
            else if (tok == "<int>")
            {
                return TokenType::UrlPattern::Dynamic::INT;
            }
            else
            {
                // throw std::invalid_argument("UrlPattern::processToken: Unknown dynamic token type '" + tok + "'");
                LOG_ERR("Unknown dynamic token type '%s'", tok.c_str());
            }
        }
        // иначе это статическй токен

        // проверяем тип: std::string или int
        // возврат int, если там число
        if (std::all_of(tok.begin(), tok.end(), ::isdigit))
        {
            return TokenType::UrlPattern::Static{std::stoi(tok)};
        }

        // возврат строки
        return TokenType::UrlPattern::Static{tok};
    }

    //--- Сравнение паттерна с url ---
    bool operator==(const Url &url, const UrlPattern &pattern)
    {
        //std::cout << "\toperator==: Compairing url '" << url
        //          << "' with pattern '" << pattern << "'\n";

        // получаем массивы токенов
        const auto &url_tokens = url.getTokens();         // TokenType::Url::Type (<string, int>)
        const auto &pattern_tokens = pattern.getTokens(); // TokenType::UrlPattern::Type (<Static<string, int>, Dynamic<STRING, INT>>)

        // если длина не равна, то они не равны
        if (url_tokens.size() != pattern_tokens.size())
            return false;

        //std::cout << "\toperator==: token size in url: " << url.m_pattern.size() << std::endl;

        // попарно сравниваем токены
        for (size_t i = 0; i < url_tokens.size(); i++)
        {
            // получаем текущий токен
            const auto &url_token = url_tokens[i];
            const auto &pattern_token = pattern_tokens[i];

            // перебираем все варианты сравнения двух токенов
            bool match = std::visit(
                [&url_token](const auto &ptok) -> bool
                {
                    // получаем типы токенов
                    using PType = std::decay_t<decltype(ptok)>;

                    // если в паттерне статический токен
                    if constexpr (std::is_same_v<PType, TokenType::UrlPattern::Static>)
                    {
                        // сравниваем variant'ы напрямую
                        return ptok == url_token;
                    }
                    // если в паттерне динамический токен
                    else if constexpr (std::is_same_v<PType, TokenType::UrlPattern::Dynamic>)
                    {
                        // Сравниваем типы перечисления
                        switch (ptok)
                        {
                        // если динамический токен шаблона - число
                        case TokenType::UrlPattern::Dynamic::INT:
                            // является ли токен url числом?
                            return std::holds_alternative<int>(url_token);

                        // если динамический токен шаблона - строка
                        case TokenType::UrlPattern::Dynamic::STRING:
                            // является ли токен url строкой?
                            return std::holds_alternative<std::string>(url_token);

                        default:
                            return false;
                        }
                    }
                    // неизвестный тип
                    else
                    {
                        return false;
                    }
                },
                pattern_token);

            // Если текущая пара токенов не совпала, то весь URL не совпадает
            if (!match)
            {
                return false;
            }
        }

        // если дошли до сюда, значит все токены совпали
        return true;
    }

    bool operator==(const UrlPattern &pattern, const Url &url)
    {
        return url == pattern;
    }
} // namespace ripc
