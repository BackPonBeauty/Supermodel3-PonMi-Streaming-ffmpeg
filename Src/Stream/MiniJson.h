/*
 * MiniJson.h
 *
 * Streaming (P6, 2026-11-16): a tiny JSON reader, enough for the Firebase host list.
 * Parses objects, arrays, strings (basic escapes; \uXXXX kept as '?'), numbers, true/false/null.
 */

#ifndef INCLUDED_STREAM_MINIJSON_H
#define INCLUDED_STREAM_MINIJSON_H

#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace MiniJson
{
  struct Value
  {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Value> arr;
    std::map<std::string, Value> obj;

    const Value *Get(const std::string &key) const
    {
      if (type != Object) return nullptr;
      auto it = obj.find(key);
      return it == obj.end() ? nullptr : &it->second;
    }
    std::string GetString(const std::string &key, const std::string &def = "") const
    {
      const Value *v = Get(key);
      return (v && v->type == String) ? v->str : def;
    }
    double GetNumber(const std::string &key, double def = 0.0) const
    {
      const Value *v = Get(key);
      return (v && v->type == Number) ? v->num : def;
    }
    bool GetBool(const std::string &key, bool def = false) const
    {
      const Value *v = Get(key);
      return (v && v->type == Bool) ? v->b : def;
    }
  };

  class Parser
  {
  public:
    explicit Parser(const std::string &s) : m_s(s) {}
    bool Parse(Value &out)
    {
      m_i = 0;
      if (!ParseValue(out)) return false;
      SkipWs();
      return m_i == m_s.size();
    }

  private:
    const std::string &m_s;
    size_t m_i = 0;

    void SkipWs() { while (m_i < m_s.size() && (m_s[m_i] == ' ' || m_s[m_i] == '\t' || m_s[m_i] == '\r' || m_s[m_i] == '\n')) m_i++; }
    bool Match(const char *lit)
    {
      size_t n = strlen(lit);
      if (m_s.compare(m_i, n, lit) != 0) return false;
      m_i += n;
      return true;
    }
    bool ParseString(std::string &out)
    {
      if (m_i >= m_s.size() || m_s[m_i] != '"') return false;
      m_i++;
      while (m_i < m_s.size())
      {
        char c = m_s[m_i++];
        if (c == '"') return true;
        if (c == '\\' && m_i < m_s.size())
        {
          char e = m_s[m_i++];
          switch (e)
          {
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'r': out += '\r'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'u': out += '?'; m_i += 4; break;
          default:  out += e; break;
          }
        }
        else
          out += c;
      }
      return false;
    }
    bool ParseValue(Value &v)
    {
      SkipWs();
      if (m_i >= m_s.size()) return false;
      char c = m_s[m_i];
      if (c == '{')
      {
        v.type = Value::Object;
        m_i++;
        SkipWs();
        if (m_i < m_s.size() && m_s[m_i] == '}') { m_i++; return true; }
        while (true)
        {
          SkipWs();
          std::string key;
          if (!ParseString(key)) return false;
          SkipWs();
          if (m_i >= m_s.size() || m_s[m_i] != ':') return false;
          m_i++;
          Value child;
          if (!ParseValue(child)) return false;
          v.obj[key] = std::move(child);
          SkipWs();
          if (m_i < m_s.size() && m_s[m_i] == ',') { m_i++; continue; }
          if (m_i < m_s.size() && m_s[m_i] == '}') { m_i++; return true; }
          return false;
        }
      }
      if (c == '[')
      {
        v.type = Value::Array;
        m_i++;
        SkipWs();
        if (m_i < m_s.size() && m_s[m_i] == ']') { m_i++; return true; }
        while (true)
        {
          Value child;
          if (!ParseValue(child)) return false;
          v.arr.push_back(std::move(child));
          SkipWs();
          if (m_i < m_s.size() && m_s[m_i] == ',') { m_i++; continue; }
          if (m_i < m_s.size() && m_s[m_i] == ']') { m_i++; return true; }
          return false;
        }
      }
      if (c == '"') { v.type = Value::String; return ParseString(v.str); }
      if (Match("true"))  { v.type = Value::Bool; v.b = true; return true; }
      if (Match("false")) { v.type = Value::Bool; v.b = false; return true; }
      if (Match("null"))  { v.type = Value::Null; return true; }
      // number
      const char *start = m_s.c_str() + m_i;
      char *end = nullptr;
      double d = strtod(start, &end);
      if (end == start) return false;
      m_i += (size_t) (end - start);
      v.type = Value::Number;
      v.num = d;
      return true;
    }
  };

  inline bool Parse(const std::string &text, Value &out)
  {
    Parser p(text);
    return p.Parse(out);
  }
}

#endif // INCLUDED_STREAM_MINIJSON_H
