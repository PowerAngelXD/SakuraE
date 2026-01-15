#ifndef FZSGBALL_STRING_HPP
#define FZSGBALL_STRING_HPP

#include <iostream>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace fzlib {
    class String {
        char *_content = nullptr;
        std::size_t _len = 0;
        std::size_t _cap = 0;

        void free() {
            delete[] _content;
            _content = nullptr;
            _cap = 0;
            _len = 0;
        }

        void resize(std::size_t new_len) {
            if (new_len <= _cap)
                return;

            std::size_t new_cap = std::max<std::size_t>(new_len, _cap * 2);
            char *new_content = new char[new_cap + 1];

            if (new_content == nullptr)
                throw std::bad_alloc();

            if (_content) {
                std::memcpy(new_content, _content, _len);
                delete[] _content;
            }

            _content = new_content;
            _cap = new_cap;
            _len = new_len;

            _content[new_len] = '\0';
        }

    public:
        using iter = char*;
        using const_iter = const char*;

        String() = default;

        String(const char *s) {
            _len = strlen(s);
            if (_len == 0) {
                resize(1);
                _content[0] = '\0';
                return;
            }
            resize(_len);
            std::memcpy(_content, s, _len);
            _content[_len] = '\0';
        }

        String(const char *s, std::size_t len) {
            _len = len;
            if (_len == 0) {
                resize(1);
                _content[0] = '\0';
                return;
            }
            resize(len);
            std::memcpy(_content, s, _len);
            _content[_len] = '\0';
        }

        String(const String &str) {
            resize(str._len);
            if (_len == 0) {
                resize(1);
                _content[0] = '\0';
                return;
            }
            std::memcpy(_content, str._content, str._len);
            _len = str._len;
            _content[_len] = '\0';
        }

        String(std::string&& str) {
            _len = str.size();
            resize(_len);
            std::memcpy(_content, str.c_str(), _len);
            _content[_len] = '\0';
        }

        String(const std::string& str) {
            _len = str.size();
            if (_len == 0) {
                resize(1);
                _content[0] = '\0';
                return;
            }
            resize(_len);
            std::memcpy(_content, str.c_str(), _len);
            _content[_len] = '\0';
        }

        String(std::string_view sv) {
            _len = sv.size();
            if (_len == 0) {
                resize(1);
                _content[0] = '\0';
                return;
            }
            resize(_len);
            std::memcpy(_content, sv.data(), _len);
            _content[_len] = '\0';
        }

        String(int count, char ch) {
            if (count <= 0) {
                _len = 0;
                _content = new char[1];
                _content[0] = '\0';
                return;
            }

            _len = static_cast<size_t>(count);
            if (_len == 0) {
                resize(1);
                _content[0] = '\0';
                return;
            }

            _content = new char[_len + 1];

            std::memset(_content, ch, _len);

            _content[_len] = '\0';
        }

        ~String() { free(); }

        // Methods

        iter begin() { return _content; }
        const_iter begin() const { return _content; }

        iter end() { return _content + _len; }
        const_iter end() const { return _content + _len; }

        // 获得当前字符串的长度
        std::size_t len() const { return _len; }
        // 获得当前对象的容量
        std::size_t cap() const { return _cap; }

        char &at(std::size_t index) {
            if (index >= _len)
                throw std::out_of_range("Cannot get a out of index value!");

            return _content[index];
        }

        String &append(const char *str) {
            std::size_t app_len = strlen(str);
            std::size_t new_len = app_len + _len;
            std::size_t old_len = _len;

            resize(new_len);

            std::memcpy(_content + old_len, str, app_len);
            _len = new_len;

            _content[_len] = '\0';

            return *this;
        }

        String &append(char ch) {
            std::size_t new_len = _len + 1;
            std::size_t old_len = _len;

            resize(new_len);

            std::memcpy(_content + old_len, &ch, 1);
            _len = new_len;

            _content[_len] = '\0';

            return *this;
        }

        String &append(const String &str) {
            std::size_t app_len = str._len;
            std::size_t new_len = app_len + _len;
            std::size_t old_len = _len;

            resize(new_len);

            std::memcpy(_content + old_len, str._content, app_len);
            _len = new_len;

            _content[_len] = '\0';

            return *this;
        }

        // 从当前字符串切出子字符串
        // @param begin 开始处，0为字符串开头
        // @param len 想要获得多长的子字符串，从开头向后截取
        String slice(std::size_t begin, std::size_t len) {
            if (begin >= _len)
                return String("");
            if (begin + len > _len) {
                len = _len - begin;
            }

            return String(_content + begin, len);
        }

        // 将 [begin, end) 这一区间内的内容删除
        String &erase(std::size_t begin, std::size_t end) {
            if (begin >= _len) {
                return *this;
            }
            if (end > _len) {
                end = _len;
            }
            if (begin >= end) {
                return *this;
            }

            std::size_t steps = _len - end;

            std::memmove(_content + begin, _content + end, steps);

            _len -= end - begin;

            _content[_len] = '\0';

            return *this;
        }

        // 从开头向后删 step 个字符
        String &erase(std::size_t step) {
            std::size_t new_len = _len - step;

            resize(new_len);

            std::memcpy(_content, _content + step, new_len);
            _len = new_len;

            _content[_len] = '\0';

            return *this;
        }

        // 按照 pattern 分割字符串，以vector形式返回
        // @param pattern 按哪个字符分割
        std::vector<String> split(char pattern) {
            std::vector<String> result;

            String tmp;
            for (std::size_t i = 0; i < _len; i++) {
                char chk = (*this)[i];
                if (chk == pattern || chk == '\0') {
                    result.push_back(tmp);
                    tmp.free();
                    continue;
                }
                tmp += chk;
            }
            
            if (tmp._len != 0)
                result.push_back(tmp);

            return result;
        }

        const char *c_str() const {
            return _content ? _content : "\0";
        }

        // Operators

        char& operator[](std::size_t index) {
            if (index >= _len)
                throw std::out_of_range("Cannot get a out of index value!");

            return _content[index];
        }

        char operator[](std::size_t index) const {
            if (index >= _len)
                throw std::out_of_range("Index out of range!");
            return _content[index];
        }

        bool operator<(const String& other) const {
            return this->_content < other._content; 
        }

        String& operator+= (const String &str) {
            append(str);
            return *this;
        }

        String& operator+= (char ch) {
            append(ch);
            return *this;
        }

        String& operator+= (const char *str) {
            append(str);
            return *this;
        }

        String operator+ (const String &str) {
            String new_str(*this);
            new_str.append(str);
            return new_str;
        }

        String operator+ (const char *str) {
            String new_str(*this);
            new_str.append(str);
            return new_str;
        }

        String& operator= (const char *str) {
            if (str == nullptr) {
                free();
                return *this;
            }
    
            if (_content == str) return *this;

            std::size_t new_l = strlen(str);
            _len = 0;
            resize(new_l);

            std::memcpy(_content, str, new_l);
            _len = new_l;
            _content[_len] = '\0';

            return *this;
        }

        String& operator= (char ch) {
            free();

            _len = 1;
            resize(_len);
            std::memcpy(_content, &ch, _len);
            _content[_len] = '\0';

            return *this;
        }

        String& operator= (const String& str) {
            if (this == &str) return *this;

            _len = 0; 
            resize(str._len); 

            if (str._len > 0) {
                std::memcpy(_content, str._content, str._len);
            }
            
            _len = str._len;
            _content[_len] = '\0';

            return *this;
        }

        bool operator== (const String& str) const {
            if (_len != str._len) return false;
            else if (std::memcmp(_content, str._content, _len) != 0) return false;
            else return true;
        }

        bool operator!= (const String& str) const {
            return !operator==(str);
        }

        // Friend

        friend std::ostream& operator<<(std::ostream &oss, const String &str) {
            if (str._content)
            {
                oss.write(str._content, str._len);
            }
            return oss;
        }

        friend std::istream& operator>>(std::istream &iss, String &str) {
            str.free();
            char ch;
            
            while (iss.get(ch) && std::isspace(ch));

            if (iss) {
                str.append(ch);
                while (iss.get(ch) && !std::isspace(ch)) {
                    str.append(ch);
                }
                if (iss) iss.putback(ch);
            }
            return iss;
        }

        friend std::istream& getline(std::istream &is, String &str, char delim = '\n') {
            str.free();
            char ch;
            while (is.get(ch)) {
                if (ch == delim) {
                    break;
                }
                str.append(ch);
            }
            return is;
        }

        friend String operator+ (const char* lhs, String &rhs) {
            String result(lhs);
            result += rhs;
            return result;
        }

        friend String operator+(const String& lhs, const char* rhs) {
            String result(lhs);
            result.append(rhs);
            return result;
        }

        friend String operator+(const String& lhs, const String& rhs) {
            String result(lhs);
            result.append(rhs);
            return result;
        }
    };
}

#endif // !FZSGBALL_STRING_HPP