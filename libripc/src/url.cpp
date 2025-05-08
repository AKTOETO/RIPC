#include "ripc/url.hpp"
#include <stdexcept>
#include <algorithm>
#include <iostream>

namespace ripc
{

    // template <typename DynTok>
    // void IUrl<DynTok>::subdivide(const std::string &str)
    // {
    //     // текущий токен
    //     std::string current;
    //     // проверка на символы <>
    //     bool in_brackets = false;
    //     // ищем символ '/' и делим по нему строку на токены
    //     for (char ch : str)
    //     {
    //         if (ch == '<')
    //         {
    //             if (in_brackets)
    //             {
    //                 // два открывающих <
    //                 throw std::invalid_argument("UrlPattern::subdivide: two '<' in a row");
    //             }
    //             in_brackets = true;

    //             if (!current.empty())
    //             {
    //                 m_pattern.emplace_back(current);
    //                 current.clear();
    //             }
    //         }
    //         else if (ch == '>')
    //         {
    //             if (!in_brackets)
    //             {
    //                 // не было открывающей скобки
    //                 throw std::invalid_argument("UrlPattern::subdivide: there was no '<' before '>'");
    //             }

    //             // Пытаемся определить тип содержимого
    //             if (current.empty())
    //             {
    //                 throw std::invalid_argument("UrlPattern::subdivide: Empty <>");
    //             }

    //             // обработка динамического токена
    //             m_pattern.emplace_back(processDynTok(current));

    //             // if (std::all_of(current.begin(), current.end(), ::isdigit))
    //             // {
    //             //     DynamicToken dyn = std::stoi(current);
    //             //     m_pattern.emplace_back(dyn);
    //             // }
    //             // else
    //             // {
    //             //     DynamicToken dyn = current;
    //             //     m_pattern.emplace_back(dyn);
    //             // }
    //             current.clear();
    //         }
    //         else if (ch == '/')
    //         {
    //             if (in_brackets)
    //             {
    //                 current += ch;
    //             }
    //             else
    //             {
    //                 if (!current.empty())
    //                 {
    //                     m_pattern.emplace_back(current);
    //                     current.clear();
    //                 }
    //             }
    //         }
    //         else
    //         {
    //             current += ch;
    //         }
    //     }
    //     // Обработка оставшихся данных
    //     if (in_brackets)
    //     {
    //         throw std::invalid_argument("Unclosed <");
    //     }

    //     if (!current.empty())
    //     {
    //         m_pattern.emplace_back(current);
    //     }
    // }

    //--- URL ---

    Url::Url(const std::string &url)
        : IUrl(url)
    {
    }

    Url::Url(const char *url)
        : IUrl(url)
    {
    }

    DynTok::Url Url::processDynTok(const std::string &tok)
    {
        if (std::all_of(tok.begin(), tok.end(), ::isdigit))
        {
            return std::stoi(tok);
        }

        return tok;
    }

    // void Url::subdivide(const std::string &str)
    // {
    //     // текущий токен
    //     std::string current;
    //     // проверка на символы <>
    //     bool in_brackets = false;
    //     // ищем символ '/' и делим по нему строку на токены
    //     for (char ch : str)
    //     {
    //         if (ch == '<')
    //         {
    //             if (in_brackets)
    //             {
    //                 // два открывающих <
    //                 throw std::invalid_argument("UrlPattern::subdivide: two '<' in a row");
    //             }
    //             in_brackets = true;

    //             if (!current.empty())
    //             {
    //                 m_pattern.emplace_back(current);
    //                 current.clear();
    //             }
    //         }
    //         else if (ch == '>')
    //         {
    //             if (!in_brackets)
    //             {
    //                 // не было открывающей скобки
    //                 throw std::invalid_argument("UrlPattern::subdivide: there was no '<' before '>'");
    //             }

    //             // Пытаемся определить тип содержимого
    //             if (current.empty())
    //             {
    //                 throw std::invalid_argument("UrlPattern::subdivide: Empty <>");
    //             }

    //             if (std::all_of(current.begin(), current.end(), ::isdigit))
    //             {
    //                 DynamicToken dyn = std::stoi(current);
    //                 m_pattern.emplace_back(dyn);
    //             }
    //             else
    //             {
    //                 DynamicToken dyn = current;
    //                 m_pattern.emplace_back(dyn);
    //             }
    //             current.clear();
    //         }
    //         else if (ch == '/')
    //         {
    //             if (in_brackets)
    //             {
    //                 current += ch;
    //             }
    //             else
    //             {
    //                 if (!current.empty())
    //                 {
    //                     m_pattern.emplace_back(current);
    //                     current.clear();
    //                 }
    //             }
    //         }
    //         else
    //         {
    //             current += ch;
    //         }
    //     }
    //     // Обработка оставшихся данных
    //     if (in_brackets)
    //     {
    //         throw std::invalid_argument("Unclosed <");
    //     }

    //     if (!current.empty())
    //     {
    //         m_pattern.emplace_back(current);
    //     }
    // }

    // bool Url::operator<(const Url &pat)
    // {
    //     return m_pattern < pat.m_pattern;
    // }

    // --- Pattern ---

    UrlPattern::UrlPattern(const std::string &pattern)
        : IUrl(pattern)
    {
    }

    UrlPattern::UrlPattern(const char *url)
        : IUrl(url)
    {
    }

    DynTok::UrlPattern UrlPattern::processDynTok(const std::string &tok)
    {
        if (tok == "string")
        {
            return DynTok::UrlPattern::STRING;
        }
        else if (tok == "int")
        {
            return DynTok::UrlPattern::INT;
        }
        else
            std::cerr << "[UrlPattern::processDynTok] Unknown type: " << tok << std::endl;
        return DynTok::UrlPattern::NONE;
    }

    // void UrlPattern::subdivide(const std::string &str)
    // {
    //     // шаблон
    //     // path/<int>/to/smth
    //     // path/<string>/to/smth2
    //     // path/asdas/addd

    //     // текущий токен
    //     std::string current;
    //     // проверка на символы <>
    //     bool in_brackets = false;
    //     // ищем символ '/' и делим по нему строку на токены
    //     for (char ch : str)
    //     {
    //         if (ch == '<')
    //         {
    //             if (in_brackets)
    //             {
    //                 // два открывающих <
    //                 throw std::invalid_argument("UrlPattern::subdivide: two '<' in a row");
    //             }
    //             in_brackets = true;

    //             if (!current.empty())
    //             {
    //                 m_pattern.emplace_back(current);
    //                 current.clear();
    //             }
    //         }
    //         else if (ch == '>')
    //         {
    //             if (!in_brackets)
    //             {
    //                 // не было открывающей скобки
    //                 throw std::invalid_argument("UrlPattern::subdivide: there was no '<' before '>'");
    //             }

    //             // Пытаемся определить тип содержимого
    //             if (current.empty())
    //             {
    //                 throw std::invalid_argument("UrlPattern::subdivide: Empty <>");
    //             }

    //             if (current == "string")
    //             {
    //                 DynamicToken dyn = DynamicToken::INT;
    //                 m_pattern.emplace_back(dyn);
    //             }
    //             else if (current == "int")
    //             {
    //                 DynamicToken dyn = DynamicToken::STRING;
    //                 m_pattern.emplace_back(dyn);
    //             }
    //             current.clear();
    //         }
    //         else if (ch == '/')
    //         {
    //             if (in_brackets)
    //             {
    //                 current += ch;
    //             }
    //             else
    //             {
    //                 if (!current.empty())
    //                 {
    //                     m_pattern.emplace_back(current);
    //                     current.clear();
    //                 }
    //             }
    //         }
    //         else
    //         {
    //             current += ch;
    //         }
    //     }
    //     // Обработка оставшихся данных
    //     if (in_brackets)
    //     {
    //         throw std::invalid_argument("Unclosed <");
    //     }

    //     if (!current.empty())
    //     {
    //         m_pattern.emplace_back(current);
    //     }
    // }

    //--- Сравнение паттерна с url ---
    bool operator==(const Url &url, const UrlPattern &pattern)
    {
        std::cout << "\toperator==: Compairing url '" << url
                  << "' with pattern '" << pattern << "'\n";
        // если длина не равна, то они не равны
        if (pattern.m_pattern.size() != url.m_pattern.size())
            return false;

        std::cout << "\toperator==: token size in url: " << url.m_pattern.size() << std::endl;

        // сравниваем каждый токен
        for (int i = 0; i < pattern.m_pattern.size(); i++)
        {
            // если там статические строки и они одинаковые
            if ((std::holds_alternative<std::string>(pattern.m_pattern[i]) &&
                 std::holds_alternative<std::string>(url.m_pattern[i])) &&
                (std::get<std::string>(pattern.m_pattern[i]) ==
                 std::get<std::string>(url.m_pattern[i])))
            {
                std::cout << "\toperator==: static url: '"
                          << std::get<std::string>(url.m_pattern[i]) << "' pattern: '"
                          << std::get<std::string>(pattern.m_pattern[i]) << "'\n";
                return true;
            }
            // если там динамический параметр
            else if (std::holds_alternative<DynTok::UrlPattern>(pattern.m_pattern[i]) &&
                     std::holds_alternative<DynTok::Url>(url.m_pattern[i]))
            {
                // если динамический параметр является строкой
                if ((std::get<DynTok::UrlPattern>(pattern.m_pattern[i]) == DynTok::UrlPattern::STRING) &&
                    std::holds_alternative<std::string>(std::get<DynTok::Url>(url.m_pattern[i])))
                {

                    std::cout << "\toperator==: dyn str url: '"
                              << std::get<std::string>(std::get<DynTok::Url>(url.m_pattern[i]))
                              << "' pattern: '"
                              << (int)std::get<DynTok::UrlPattern>(pattern.m_pattern[i]) << "'\n";
                    return true;
                }
                // если динамический параметр является целым числом
                else if ((std::get<DynTok::UrlPattern>(pattern.m_pattern[i]) == DynTok::UrlPattern::INT) &&
                         std::holds_alternative<int>(std::get<DynTok::Url>(url.m_pattern[i])))
                {

                    std::cout << "\toperator==: dyn int url: '"
                              << std::get<int>(std::get<DynTok::Url>(url.m_pattern[i]))
                              << "' pattern: '"
                              << (int)std::get<DynTok::UrlPattern>(pattern.m_pattern[i]) << "'\n";
                    return true;
                }

                else
                {
                    std::cerr << "[Url == UrlPattern] Unknown dynamic param\n";
                    return false;
                }
            }
            else
            {
                std::cerr << "[Url == UrlPattern] Unknown param\n";
                return false;
            }
        }

        return false;
    }

    bool operator==(const UrlPattern &pattern, const Url &url)
    {
        return url == pattern;
    }
    bool operator<(const UrlPattern &p1, const UrlPattern &p2)
    {
        return p1.m_pattern < p2.m_pattern;
    }
} // namespace ripc
