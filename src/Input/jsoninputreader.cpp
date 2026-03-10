/* EPANET 3
 *
 * Copyright (c) 2016 Open Water Analytics
 * Licensed under the terms of the MIT License (see the LICENSE file for details).
 *
 */

#include "jsoninputreader.h"
#include "inputreader.h"
#include "inputparser.h"
#include "Core/network.h"
#include "Core/error.h"
#include "Elements/node.h"
#include "Elements/link.h"
#include "Elements/pattern.h"
#include "Utilities/utilities.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <initializer_list>
#include <map>
#include <sstream>
#include <vector>

using namespace std;

namespace
{

class JsonValue
{
  public:
    enum Type {NUL, BOOL, NUMBER, STRING, ARRAY, OBJECT};

    JsonValue() : type(NUL), boolValue(false), numberValue(0.0) {}
    explicit JsonValue(bool value) : type(BOOL), boolValue(value), numberValue(0.0) {}
    explicit JsonValue(double value) : type(NUMBER), boolValue(false), numberValue(value) {}
    explicit JsonValue(const string& value) : type(STRING), boolValue(false), numberValue(0.0), stringValue(value) {}

    static JsonValue array()
    {
        JsonValue value;
        value.type = ARRAY;
        return value;
    }

    static JsonValue object()
    {
        JsonValue value;
        value.type = OBJECT;
        return value;
    }

    bool isNull()   const { return type == NUL; }
    bool isBool()   const { return type == BOOL; }
    bool isNumber() const { return type == NUMBER; }
    bool isString() const { return type == STRING; }
    bool isArray()  const { return type == ARRAY; }
    bool isObject() const { return type == OBJECT; }

    Type type;
    bool boolValue;
    double numberValue;
    string stringValue;
    vector<JsonValue> arrayValues;
    map<string, JsonValue> objectValues;
};

class JsonParser
{
  public:
    explicit JsonParser(const string& text_) : text(text_), pos(0) {}

    JsonValue parse()
    {
        skipWhitespace();
        JsonValue value = parseValue();
        skipWhitespace();
        if ( pos != text.size() )
        {
            error(" unexpected trailing text");
        }
        return value;
    }

  private:
    const string& text;
    size_t pos;

    void skipWhitespace()
    {
        while ( pos < text.size() && isspace((unsigned char)text[pos]) ) pos++;
    }

    char peek() const
    {
        if ( pos >= text.size() ) return '\0';
        return text[pos];
    }

    char get()
    {
        if ( pos >= text.size() ) error(" unexpected end of input");
        return text[pos++];
    }

    void error(const string& message) const
    {
        throw InputError(InputError::UNSPECIFIED,
            "JSON parse error at position " + Utilities::to_string(pos) + ":" + message);
    }

    JsonValue parseValue()
    {
        char ch = peek();
        if ( ch == '{' ) return parseObject();
        if ( ch == '[' ) return parseArray();
        if ( ch == '"' ) return JsonValue(parseString());
        if ( ch == '-' || isdigit((unsigned char)ch) ) return JsonValue(parseNumber());
        if ( ch == 't' )
        {
            parseLiteral("true");
            return JsonValue(true);
        }
        if ( ch == 'f' )
        {
            parseLiteral("false");
            return JsonValue(false);
        }
        if ( ch == 'n' )
        {
            parseLiteral("null");
            return JsonValue();
        }
        error(" invalid value");
        return JsonValue();
    }

    JsonValue parseObject()
    {
        JsonValue objectValue = JsonValue::object();
        get();
        skipWhitespace();
        if ( peek() == '}' )
        {
            get();
            return objectValue;
        }

        while ( true )
        {
            skipWhitespace();
            if ( peek() != '"' ) error(" expected object key");
            string key = parseString();
            skipWhitespace();
            if ( get() != ':' ) error(" expected ':' after object key");
            skipWhitespace();
            objectValue.objectValues[key] = parseValue();
            skipWhitespace();
            char ch = get();
            if ( ch == '}' ) break;
            if ( ch != ',' ) error(" expected ',' or '}'");
        }
        return objectValue;
    }

    JsonValue parseArray()
    {
        JsonValue arrayValue = JsonValue::array();
        get();
        skipWhitespace();
        if ( peek() == ']' )
        {
            get();
            return arrayValue;
        }

        while ( true )
        {
            skipWhitespace();
            arrayValue.arrayValues.push_back(parseValue());
            skipWhitespace();
            char ch = get();
            if ( ch == ']' ) break;
            if ( ch != ',' ) error(" expected ',' or ']'");
        }
        return arrayValue;
    }

    string parseString()
    {
        if ( get() != '"' ) error(" expected string");
        string result;
        while ( true )
        {
            if ( pos >= text.size() ) error(" unterminated string");
            char ch = get();
            if ( ch == '"' ) return result;
            if ( ch != '\\' )
            {
                result.push_back(ch);
                continue;
            }

            if ( pos >= text.size() ) error(" unterminated escape sequence");
            char esc = get();
            switch (esc)
            {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': appendUnicode(result); break;
            default: error(" invalid escape sequence");
            }
        }
    }

    void appendUnicode(string& out)
    {
        if ( pos + 4 > text.size() ) error(" incomplete unicode escape");
        unsigned int code = 0;
        for (int i = 0; i < 4; i++)
        {
            char ch = text[pos++];
            code <<= 4;
            if ( ch >= '0' && ch <= '9' ) code += ch - '0';
            else if ( ch >= 'a' && ch <= 'f' ) code += ch - 'a' + 10;
            else if ( ch >= 'A' && ch <= 'F' ) code += ch - 'A' + 10;
            else error(" invalid unicode escape");
        }

        if ( code <= 0x7F )
        {
            out.push_back((char)code);
        }
        else if ( code <= 0x7FF )
        {
            out.push_back((char)(0xC0 | (code >> 6)));
            out.push_back((char)(0x80 | (code & 0x3F)));
        }
        else
        {
            out.push_back((char)(0xE0 | (code >> 12)));
            out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (code & 0x3F)));
        }
    }

    double parseNumber()
    {
        size_t start = pos;
        if ( peek() == '-' ) pos++;

        if ( peek() == '0' ) pos++;
        else
        {
            if ( !isdigit((unsigned char)peek()) ) error(" invalid number");
            while ( isdigit((unsigned char)peek()) ) pos++;
        }

        if ( peek() == '.' )
        {
            pos++;
            if ( !isdigit((unsigned char)peek()) ) error(" invalid number");
            while ( isdigit((unsigned char)peek()) ) pos++;
        }

        if ( peek() == 'e' || peek() == 'E' )
        {
            pos++;
            if ( peek() == '+' || peek() == '-' ) pos++;
            if ( !isdigit((unsigned char)peek()) ) error(" invalid exponent");
            while ( isdigit((unsigned char)peek()) ) pos++;
        }

        char* endPtr = nullptr;
        double value = strtod(text.substr(start, pos - start).c_str(), &endPtr);
        if ( endPtr == nullptr || *endPtr != '\0' ) error(" invalid number");
        return value;
    }

    void parseLiteral(const string& literal)
    {
        if ( text.compare(pos, literal.size(), literal) != 0 )
        {
            error(" invalid literal");
        }
        pos += literal.size();
    }
};

string normalizeKey(const string& text)
{
    string result;
    for (char ch : text)
    {
        if ( isalnum((unsigned char)ch) ) result.push_back((char)toupper((unsigned char)ch));
    }
    return result;
}

string keyToLineToken(const string& text)
{
    string result;
    bool lastWasSpace = true;
    for (char ch : text)
    {
        if ( isalnum((unsigned char)ch) )
        {
            result.push_back((char)toupper((unsigned char)ch));
            lastWasSpace = false;
        }
        else if ( !lastWasSpace )
        {
            result.push_back(' ');
            lastWasSpace = true;
        }
    }
    while ( !result.empty() && result[result.size() - 1] == ' ' ) result.erase(result.size() - 1);
    return result;
}

string formatNumber(double value)
{
    ostringstream out;
    out << setprecision(15) << value;
    return out.str();
}

string scalarToToken(const JsonValue& value, bool boolAsYesNo = true)
{
    if ( value.isString() ) return value.stringValue;
    if ( value.isNumber() ) return formatNumber(value.numberValue);
    if ( value.isBool() ) return value.boolValue ? (boolAsYesNo ? "YES" : "true")
                                                 : (boolAsYesNo ? "NO" : "false");
    if ( value.isNull() ) return "";
    throw InputError(InputError::UNSPECIFIED,
        "JSON value must be a string, number, or boolean");
}

const JsonValue* findMember(const JsonValue& objectValue,
                            initializer_list<const char*> aliases)
{
    if ( !objectValue.isObject() ) return nullptr;
    for (const char* alias : aliases)
    {
        string normalizedAlias = normalizeKey(alias);
        for (auto const& item : objectValue.objectValues)
        {
            if ( normalizeKey(item.first) == normalizedAlias ) return &item.second;
        }
    }
    return nullptr;
}

string requireToken(const JsonValue& objectValue,
                    initializer_list<const char*> aliases,
                    const string& context)
{
    const JsonValue* value = findMember(objectValue, aliases);
    if ( value == nullptr )
    {
        throw InputError(InputError::UNSPECIFIED,
            "JSON input missing required field in " + context);
    }
    return scalarToToken(*value, false);
}

string optionalToken(const JsonValue& objectValue,
                     initializer_list<const char*> aliases,
                     const string& defaultValue = "",
                     bool boolAsYesNo = false)
{
    const JsonValue* value = findMember(objectValue, aliases);
    if ( value == nullptr || value->isNull() ) return defaultValue;
    return scalarToToken(*value, boolAsYesNo);
}

bool hasMember(const JsonValue& objectValue, initializer_list<const char*> aliases)
{
    return findMember(objectValue, aliases) != nullptr;
}

void appendOptionLine(ostringstream& out,
                      const string& keyword,
                      const string& value,
                      const string& value2 = "")
{
    out << keyword;
    if ( !value.empty() ) out << ' ' << value;
    if ( !value2.empty() ) out << ' ' << value2;
    out << "\n";
}

vector<const JsonValue*> collectEntries(const JsonValue& sectionValue,
                                        const string& sectionName)
{
    vector<const JsonValue*> entries;
    if ( sectionValue.isArray() )
    {
        for (const JsonValue& item : sectionValue.arrayValues)
        {
            if ( !item.isObject() )
            {
                throw InputError(InputError::UNSPECIFIED,
                    "JSON section " + sectionName + " must contain objects or raw lines");
            }
            entries.push_back(&item);
        }
    }
    else if ( sectionValue.isObject() )
    {
        entries.push_back(&sectionValue);
    }
    else
    {
        throw InputError(InputError::UNSPECIFIED,
            "JSON section " + sectionName + " must be an object or array");
    }
    return entries;
}

bool tryAppendRawLines(ostringstream& out, const JsonValue& sectionValue)
{
    if ( sectionValue.isString() )
    {
        out << sectionValue.stringValue << "\n";
        return true;
    }

    if ( sectionValue.isObject() )
    {
        const JsonValue* lines = findMember(sectionValue, {"lines", "raw", "entries"});
        if ( lines != nullptr ) return tryAppendRawLines(out, *lines);
        return false;
    }

    if ( !sectionValue.isArray() ) return false;
    for (const JsonValue& item : sectionValue.arrayValues)
    {
        if ( !item.isString() ) return false;
    }
    for (const JsonValue& item : sectionValue.arrayValues)
    {
        out << item.stringValue << "\n";
    }
    return true;
}

string joinTokens(const vector<string>& tokens)
{
    ostringstream out;
    for (size_t i = 0; i < tokens.size(); i++)
    {
        if ( i > 0 ) out << ' ';
        out << tokens[i];
    }
    return out.str();
}

void flattenObjectLines(ostringstream& out,
                        const vector<string>& prefix,
                        const JsonValue& value)
{
    if ( value.isNull() ) return;

    if ( value.isString() || value.isNumber() || value.isBool() )
    {
        vector<string> tokens = prefix;
        tokens.push_back(scalarToToken(value));
        out << joinTokens(tokens) << "\n";
        return;
    }

    if ( value.isArray() )
    {
        if ( value.arrayValues.empty() ) return;
        for (const JsonValue& item : value.arrayValues)
        {
            if ( item.isObject() || item.isArray() )
            {
                throw InputError(InputError::UNSPECIFIED,
                    "JSON array values in options-like sections must contain only scalars");
            }
        }
        vector<string> tokens = prefix;
        for (const JsonValue& item : value.arrayValues)
        {
            tokens.push_back(scalarToToken(item));
        }
        out << joinTokens(tokens) << "\n";
        return;
    }

    for (auto const& item : value.objectValues)
    {
        vector<string> nextPrefix = prefix;
        string keyToken = keyToLineToken(item.first);
        if ( keyToken.empty() ) continue;

        vector<string> splitKey = Utilities::split(keyToken);
        nextPrefix.insert(nextPrefix.end(), splitKey.begin(), splitKey.end());
        flattenObjectLines(out, nextPrefix, item.second);
    }
}

void appendSimpleSectionHeader(ostringstream& out, const string& name)
{
    out << name << "\n";
}

void appendTitleSection(ostringstream& out, const JsonValue& sectionValue)
{
    if ( sectionValue.isString() )
    {
        out << sectionValue.stringValue << "\n";
        return;
    }
    if ( sectionValue.isArray() )
    {
        for (const JsonValue& item : sectionValue.arrayValues)
        {
            out << scalarToToken(item, false) << "\n";
        }
        return;
    }
    throw InputError(InputError::UNSPECIFIED,
        "JSON title section must be a string or array of strings");
}

void appendJunctions(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "junctions");
    for (const JsonValue* entry : entries)
    {
        string id = requireToken(*entry, {"id", "name"}, "junction");
        string elevation = requireToken(*entry, {"elevation", "elev"}, "junction");
        string demand = optionalToken(*entry, {"demand", "baseDemand", "base"});
        string pattern = optionalToken(*entry, {"pattern", "patternId"});
        out << id << ' ' << elevation;
        if ( !demand.empty() || !pattern.empty() ) out << ' ' << (demand.empty() ? "0" : demand);
        if ( !pattern.empty() ) out << ' ' << pattern;
        out << "\n";
    }
}

void appendReservoirs(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "reservoirs");
    for (const JsonValue* entry : entries)
    {
        string id = requireToken(*entry, {"id", "name"}, "reservoir");
        string head = requireToken(*entry, {"head", "elevation", "elev"}, "reservoir");
        string pattern = optionalToken(*entry, {"pattern", "headPattern", "patternId"});
        out << id << ' ' << head;
        if ( !pattern.empty() ) out << ' ' << pattern;
        out << "\n";
    }
}

void appendTanks(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "tanks");
    for (const JsonValue* entry : entries)
    {
        string id = requireToken(*entry, {"id", "name"}, "tank");
        string elevation = requireToken(*entry, {"elevation", "elev"}, "tank");
        string initLevel = requireToken(*entry, {"initialLevel", "initLevel", "initialDepth", "initDepth"}, "tank");
        string minLevel = requireToken(*entry, {"minimumLevel", "minLevel", "minimumDepth", "minDepth"}, "tank");
        string maxLevel = requireToken(*entry, {"maximumLevel", "maxLevel", "maximumDepth", "maxDepth"}, "tank");
        string diameter = requireToken(*entry, {"diameter"}, "tank");
        string minVolume = requireToken(*entry, {"minimumVolume", "minVolume"}, "tank");
        string curve = optionalToken(*entry, {"volumeCurve", "curve", "volumeCurveId"});

        out << id << ' ' << elevation << ' ' << initLevel << ' ' << minLevel << ' '
            << maxLevel << ' ' << diameter << ' ' << minVolume;
        if ( !curve.empty() ) out << ' ' << curve;
        out << "\n";
    }
}

void appendPipes(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "pipes");
    for (const JsonValue* entry : entries)
    {
        string id = requireToken(*entry, {"id", "name"}, "pipe");
        string from = requireToken(*entry, {"from", "fromNode", "startNode", "node1"}, "pipe");
        string to = requireToken(*entry, {"to", "toNode", "endNode", "node2"}, "pipe");
        string length = requireToken(*entry, {"length"}, "pipe");
        string diameter = requireToken(*entry, {"diameter"}, "pipe");
        string roughness = requireToken(*entry, {"roughness"}, "pipe");
        string minorLoss = optionalToken(*entry, {"minorLoss", "lossCoeff", "lossCoefficient"});
        string status = optionalToken(*entry, {"status", "initialStatus"});

        out << id << ' ' << from << ' ' << to << ' ' << length << ' '
            << diameter << ' ' << roughness;
        if ( !minorLoss.empty() || !status.empty() ) out << ' ' << (minorLoss.empty() ? "0" : minorLoss);
        if ( !status.empty() ) out << ' ' << status;
        out << "\n";
    }
}

void appendPumps(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "pumps");
    for (const JsonValue* entry : entries)
    {
        string id = requireToken(*entry, {"id", "name"}, "pump");
        string from = requireToken(*entry, {"from", "fromNode", "startNode", "node1"}, "pump");
        string to = requireToken(*entry, {"to", "toNode", "endNode", "node2"}, "pump");
        string power = optionalToken(*entry, {"power", "horsepower"});
        string head = optionalToken(*entry, {"head", "headCurve", "curve", "curveId"});
        string speed = optionalToken(*entry, {"speed"});
        string pattern = optionalToken(*entry, {"pattern", "speedPattern", "patternId"});

        if ( power.empty() && head.empty() && speed.empty() && pattern.empty() )
        {
            throw InputError(InputError::UNSPECIFIED,
                "JSON pump entries must define at least one property");
        }

        out << id << ' ' << from << ' ' << to;
        if ( !power.empty() ) out << " POWER " << power;
        if ( !head.empty() ) out << " HEAD " << head;
        if ( !speed.empty() ) out << " SPEED " << speed;
        if ( !pattern.empty() ) out << " PATTERN " << pattern;
        out << "\n";
    }
}

void appendValves(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "valves");
    for (const JsonValue* entry : entries)
    {
        string id = requireToken(*entry, {"id", "name"}, "valve");
        string from = requireToken(*entry, {"from", "fromNode", "startNode", "node1"}, "valve");
        string to = requireToken(*entry, {"to", "toNode", "endNode", "node2"}, "valve");
        string diameter = requireToken(*entry, {"diameter"}, "valve");
        string type = requireToken(*entry, {"type", "valveType"}, "valve");
        string setting = requireToken(*entry, {"setting", "initialSetting", "curve", "curveId"}, "valve");
        string minorLoss = optionalToken(*entry, {"minorLoss", "lossCoeff", "lossCoefficient"});

        out << id << ' ' << from << ' ' << to << ' ' << diameter << ' ' << type << ' ' << setting;
        if ( !minorLoss.empty() ) out << ' ' << minorLoss;
        out << "\n";
    }
}

void appendPatterns(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "patterns");
    for (const JsonValue* entry : entries)
    {
        string id = requireToken(*entry, {"id", "name"}, "pattern");
        string type = Utilities::upperCase(optionalToken(*entry, {"type"}, "FIXED"));
        string interval = optionalToken(*entry, {"interval", "timeInterval"});
        const JsonValue* factors = findMember(*entry, {"factors", "values"});
        const JsonValue* points = findMember(*entry, {"points", "entries"});

        if ( type == "VARIABLE" )
        {
            out << id << " VARIABLE\n";
            if ( points != nullptr )
            {
                if ( !points->isArray() )
                {
                    throw InputError(InputError::UNSPECIFIED,
                        "JSON variable pattern points must be an array");
                }
                out << id;
                for (const JsonValue& point : points->arrayValues)
                {
                    if ( point.isObject() )
                    {
                        string time = requireToken(point, {"time"}, "pattern point");
                        string factor = requireToken(point, {"factor", "value"}, "pattern point");
                        out << ' ' << time << ' ' << factor;
                    }
                    else if ( point.isArray() && point.arrayValues.size() == 2 )
                    {
                        out << ' ' << scalarToToken(point.arrayValues[0], false)
                            << ' ' << scalarToToken(point.arrayValues[1], false);
                    }
                    else
                    {
                        throw InputError(InputError::UNSPECIFIED,
                            "JSON variable pattern points must contain time/factor pairs");
                    }
                }
                out << "\n";
            }
        }
        else
        {
            if ( !interval.empty() ) out << id << " FIXED " << interval << "\n";
            if ( factors != nullptr )
            {
                if ( !factors->isArray() )
                {
                    throw InputError(InputError::UNSPECIFIED,
                        "JSON fixed pattern factors must be an array");
                }
                out << id;
                for (const JsonValue& factor : factors->arrayValues)
                {
                    out << ' ' << scalarToToken(factor, false);
                }
                out << "\n";
            }
            else if ( interval.empty() )
            {
                throw InputError(InputError::UNSPECIFIED,
                    "JSON fixed patterns must define factors or an interval");
            }
        }
    }
}

void appendCurves(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "curves");
    for (const JsonValue* entry : entries)
    {
        string id = requireToken(*entry, {"id", "name"}, "curve");
        string type = optionalToken(*entry, {"type", "curveType"});
        const JsonValue* points = findMember(*entry, {"points", "data"});

        if ( !type.empty() ) out << id << ' ' << type << "\n";
        if ( points != nullptr )
        {
            if ( !points->isArray() )
            {
                throw InputError(InputError::UNSPECIFIED,
                    "JSON curve points must be an array");
            }
            out << id;
            for (const JsonValue& point : points->arrayValues)
            {
                if ( point.isObject() )
                {
                    string x = requireToken(point, {"x"}, "curve point");
                    string y = requireToken(point, {"y"}, "curve point");
                    out << ' ' << x << ' ' << y;
                }
                else if ( point.isArray() && point.arrayValues.size() == 2 )
                {
                    out << ' ' << scalarToToken(point.arrayValues[0], false)
                        << ' ' << scalarToToken(point.arrayValues[1], false);
                }
                else
                {
                    throw InputError(InputError::UNSPECIFIED,
                        "JSON curve points must contain x/y pairs");
                }
            }
            out << "\n";
        }
        else if ( type.empty() )
        {
            throw InputError(InputError::UNSPECIFIED,
                "JSON curves must define a type or point data");
        }
    }
}

void appendControls(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "controls");
    for (const JsonValue* entry : entries)
    {
        string link = requireToken(*entry, {"link", "id", "name"}, "control");
        string action = optionalToken(*entry, {"status", "setting"});
        if ( action.empty() )
        {
            if ( hasMember(*entry, {"setting"}) ) action = requireToken(*entry, {"setting"}, "control");
            else throw InputError(InputError::UNSPECIFIED,
                "JSON controls must define a status or setting");
        }

        out << "LINK " << link << ' ' << action;

        const JsonValue* condition = findMember(*entry, {"if", "condition"});
        const JsonValue* at = findMember(*entry, {"at", "timeCondition"});

        if ( condition != nullptr )
        {
            if ( !condition->isObject() )
            {
                throw InputError(InputError::UNSPECIFIED,
                    "JSON control condition must be an object");
            }
            string node = requireToken(*condition, {"node", "id", "name"}, "control condition");
            string direction;
            string value;
            if ( hasMember(*condition, {"above"}) )
            {
                direction = "ABOVE";
                value = requireToken(*condition, {"above"}, "control condition");
            }
            else if ( hasMember(*condition, {"below"}) )
            {
                direction = "BELOW";
                value = requireToken(*condition, {"below"}, "control condition");
            }
            else
            {
                direction = Utilities::upperCase(requireToken(*condition, {"direction", "level"}, "control condition"));
                value = requireToken(*condition, {"value", "setting"}, "control condition");
            }
            out << " IF NODE " << node << ' ' << direction << ' ' << value;
        }
        else if ( at != nullptr )
        {
            if ( !at->isObject() )
            {
                throw InputError(InputError::UNSPECIFIED,
                    "JSON control time condition must be an object");
            }
            if ( hasMember(*at, {"time", "elapsed"}) )
            {
                out << " AT TIME " << requireToken(*at, {"time", "elapsed"}, "control time");
            }
            else if ( hasMember(*at, {"clockTime", "clocktime", "clock"}) )
            {
                out << " AT CLOCKTIME " << requireToken(*at, {"clockTime", "clocktime", "clock"}, "control clock time");
            }
            else
            {
                throw InputError(InputError::UNSPECIFIED,
                    "JSON control time conditions must define time or clockTime");
            }
        }
        else if ( hasMember(*entry, {"node"}) )
        {
            string node = requireToken(*entry, {"node"}, "control");
            if ( hasMember(*entry, {"above"}) )
            {
                out << " IF NODE " << node << " ABOVE "
                    << requireToken(*entry, {"above"}, "control");
            }
            else if ( hasMember(*entry, {"below"}) )
            {
                out << " IF NODE " << node << " BELOW "
                    << requireToken(*entry, {"below"}, "control");
            }
            else
            {
                throw InputError(InputError::UNSPECIFIED,
                    "JSON controls with node triggers must define above or below");
            }
        }
        else if ( hasMember(*entry, {"time", "elapsed"}) )
        {
            out << " AT TIME " << requireToken(*entry, {"time", "elapsed"}, "control");
        }
        else if ( hasMember(*entry, {"clockTime", "clocktime", "clock"}) )
        {
            out << " AT CLOCKTIME " << requireToken(*entry, {"clockTime", "clocktime", "clock"}, "control");
        }
        else
        {
            throw InputError(InputError::UNSPECIFIED,
                "JSON controls must define a trigger condition");
        }

        out << "\n";
    }
}

void appendEmitters(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "emitters");
    for (const JsonValue* entry : entries)
    {
        string node = requireToken(*entry, {"node", "id", "name"}, "emitter");
        string coeff = requireToken(*entry, {"coefficient", "coeff", "flowCoefficient"}, "emitter");
        string exponent = optionalToken(*entry, {"exponent", "flowExponent"});
        string pattern = optionalToken(*entry, {"pattern", "patternId"});
        if ( !pattern.empty() && exponent.empty() )
        {
            throw InputError(InputError::UNSPECIFIED,
                "JSON emitters with patterns must also define an exponent or use raw lines");
        }
        out << node << ' ' << coeff;
        if ( !exponent.empty() ) out << ' ' << exponent;
        if ( !pattern.empty() ) out << ' ' << pattern;
        out << "\n";
    }
}

void appendDemands(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "demands");
    for (const JsonValue* entry : entries)
    {
        string node = requireToken(*entry, {"node", "id", "name"}, "demand");
        string base = requireToken(*entry, {"base", "demand", "baseDemand"}, "demand");
        string pattern = optionalToken(*entry, {"pattern", "patternId"});
        out << node << ' ' << base;
        if ( !pattern.empty() ) out << ' ' << pattern;
        out << "\n";
    }
}

void appendStatus(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "status");
    for (const JsonValue* entry : entries)
    {
        string link = requireToken(*entry, {"link", "id", "name"}, "status");
        string value;
        if ( hasMember(*entry, {"status"}) ) value = requireToken(*entry, {"status"}, "status");
        else value = requireToken(*entry, {"setting", "value"}, "status");
        out << link << ' ' << value << "\n";
    }
}

void appendLeakage(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "leakage");
    for (const JsonValue* entry : entries)
    {
        string link = requireToken(*entry, {"link", "id", "name"}, "leakage");
        string coeff1 = requireToken(*entry, {"coeff1", "coefficient1", "parameter1"}, "leakage");
        string coeff2 = requireToken(*entry, {"coeff2", "coefficient2", "parameter2"}, "leakage");
        out << link << ' ' << coeff1 << ' ' << coeff2 << "\n";
    }
}

void appendEnergy(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    if ( sectionValue.isArray() )
    {
        throw InputError(InputError::UNSPECIFIED,
            "JSON energy section must be an object or raw lines");
    }

    const JsonValue* global = findMember(sectionValue, {"global"});
    if ( global != nullptr )
    {
        if ( !global->isObject() )
        {
            throw InputError(InputError::UNSPECIFIED,
                "JSON energy global section must be an object");
        }
        if ( hasMember(*global, {"price"}) )
        {
            out << "GLOBAL PRICE " << requireToken(*global, {"price"}, "energy global") << "\n";
        }
        if ( hasMember(*global, {"pattern", "pricePattern"}) )
        {
            out << "GLOBAL PATTERN " << requireToken(*global, {"pattern", "pricePattern"}, "energy global") << "\n";
        }
        if ( hasMember(*global, {"efficiency", "effic"}) )
        {
            out << "GLOBAL EFFIC " << requireToken(*global, {"efficiency", "effic"}, "energy global") << "\n";
        }
    }

    if ( hasMember(sectionValue, {"demandCharge", "demand"}) )
    {
        out << "DEMAND CHARGE " << requireToken(sectionValue, {"demandCharge", "demand"}, "energy") << "\n";
    }

    const JsonValue* pumps = findMember(sectionValue, {"pumps", "pump"});
    if ( pumps != nullptr )
    {
        vector<const JsonValue*> entries = collectEntries(*pumps, "energy pumps");
        for (const JsonValue* entry : entries)
        {
            string id = requireToken(*entry, {"id", "name", "pump"}, "energy pump");
            if ( hasMember(*entry, {"price"}) )
            {
                out << "PUMP " << id << " PRICE "
                    << requireToken(*entry, {"price"}, "energy pump") << "\n";
            }
            if ( hasMember(*entry, {"pattern", "pricePattern"}) )
            {
                out << "PUMP " << id << " PATTERN "
                    << requireToken(*entry, {"pattern", "pricePattern"}, "energy pump") << "\n";
            }
            if ( hasMember(*entry, {"efficiencyCurve", "effic", "efficiency"}) )
            {
                out << "PUMP " << id << " EFFIC "
                    << requireToken(*entry, {"efficiencyCurve", "effic", "efficiency"}, "energy pump") << "\n";
            }
        }
    }
}

void appendQuality(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "quality");
    for (const JsonValue* entry : entries)
    {
        string node = requireToken(*entry, {"node", "id", "name"}, "quality");
        string value = requireToken(*entry, {"quality", "initialQuality", "value"}, "quality");
        out << node << ' ' << value << "\n";
    }
}

void appendSources(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "sources");
    for (const JsonValue* entry : entries)
    {
        string node = requireToken(*entry, {"node", "id", "name"}, "source");
        string type = requireToken(*entry, {"type", "sourceType"}, "source");
        string strength = requireToken(*entry, {"strength", "baseline", "value"}, "source");
        string pattern = optionalToken(*entry, {"pattern", "patternId"});
        out << node << ' ' << type << ' ' << strength;
        if ( !pattern.empty() ) out << ' ' << pattern;
        out << "\n";
    }
}

void appendReactions(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    if ( !sectionValue.isObject() )
    {
        throw InputError(InputError::UNSPECIFIED,
            "JSON reactions section must be an object or raw lines");
    }

    const JsonValue* order = findMember(sectionValue, {"order"});
    if ( order != nullptr )
    {
        if ( hasMember(*order, {"bulk"}) )
        {
            out << "ORDER BULK " << requireToken(*order, {"bulk"}, "reactions order") << "\n";
        }
        if ( hasMember(*order, {"wall"}) )
        {
            out << "ORDER WALL " << requireToken(*order, {"wall"}, "reactions order") << "\n";
        }
        if ( hasMember(*order, {"tank"}) )
        {
            out << "ORDER TANK " << requireToken(*order, {"tank"}, "reactions order") << "\n";
        }
    }

    const JsonValue* global = findMember(sectionValue, {"global"});
    if ( global != nullptr )
    {
        if ( hasMember(*global, {"bulk"}) )
        {
            out << "GLOBAL BULK " << requireToken(*global, {"bulk"}, "reactions global") << "\n";
        }
        if ( hasMember(*global, {"wall"}) )
        {
            out << "GLOBAL WALL " << requireToken(*global, {"wall"}, "reactions global") << "\n";
        }
    }

    if ( hasMember(sectionValue, {"limiting", "limitingConcentration"}) )
    {
        out << "LIMITING CONCENTRATION "
            << requireToken(sectionValue, {"limiting", "limitingConcentration"}, "reactions")
            << "\n";
    }
    if ( hasMember(sectionValue, {"roughness", "roughnessFactor"}) )
    {
        out << "ROUGHNESS CORRELATION "
            << requireToken(sectionValue, {"roughness", "roughnessFactor"}, "reactions")
            << "\n";
    }

    const JsonValue* pipes = findMember(sectionValue, {"pipes", "links"});
    if ( pipes != nullptr )
    {
        vector<const JsonValue*> entries = collectEntries(*pipes, "reaction pipes");
        for (const JsonValue* entry : entries)
        {
            string id = requireToken(*entry, {"id", "name", "link", "pipe"}, "reaction pipe");
            if ( hasMember(*entry, {"bulk"}) )
            {
                out << "BULK " << id << ' ' << requireToken(*entry, {"bulk"}, "reaction pipe") << "\n";
            }
            if ( hasMember(*entry, {"wall"}) )
            {
                out << "WALL " << id << ' ' << requireToken(*entry, {"wall"}, "reaction pipe") << "\n";
            }
        }
    }

    const JsonValue* tanks = findMember(sectionValue, {"tanks", "nodes"});
    if ( tanks != nullptr )
    {
        vector<const JsonValue*> entries = collectEntries(*tanks, "reaction tanks");
        for (const JsonValue* entry : entries)
        {
            string id = requireToken(*entry, {"id", "name", "node", "tank"}, "reaction tank");
            string coeff = requireToken(*entry, {"bulk", "coefficient", "value"}, "reaction tank");
            out << "TANK " << id << ' ' << coeff << "\n";
        }
    }
}

void appendMixing(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "mixing");
    for (const JsonValue* entry : entries)
    {
        string tank = requireToken(*entry, {"tank", "node", "id", "name"}, "mixing");
        string model = requireToken(*entry, {"model", "type"}, "mixing");
        string fraction = optionalToken(*entry, {"fraction", "mixingFraction"});
        out << tank << ' ' << model;
        if ( !fraction.empty() ) out << ' ' << fraction;
        out << "\n";
    }
}

void appendGenericFlattenedSection(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    if ( !sectionValue.isObject() )
    {
        throw InputError(InputError::UNSPECIFIED,
            "JSON section must be an object or raw lines");
    }
    vector<string> prefix;
    flattenObjectLines(out, prefix, sectionValue);
}

void appendOptionsSection(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    if ( !sectionValue.isObject() )
    {
        throw InputError(InputError::UNSPECIFIED,
            "JSON options section must be an object or raw lines");
    }

    if ( hasMember(sectionValue, {"units", "flowUnits"}) )
    {
        appendOptionLine(out, "FLOW_UNITS",
            requireToken(sectionValue, {"units", "flowUnits"}, "options"));
    }
    if ( hasMember(sectionValue, {"pressure", "pressureUnits"}) )
    {
        appendOptionLine(out, "PRESSURE_UNITS",
            requireToken(sectionValue, {"pressure", "pressureUnits"}, "options"));
    }
    if ( hasMember(sectionValue, {"headloss", "headlossModel"}) )
    {
        appendOptionLine(out, "HEADLOSS_MODEL",
            requireToken(sectionValue, {"headloss", "headlossModel"}, "options"));
    }

    const JsonValue* demand = findMember(sectionValue, {"demand"});
    if ( demand != nullptr )
    {
        if ( !demand->isObject() ) throw InputError(InputError::UNSPECIFIED,
            "JSON options demand section must be an object");
        if ( hasMember(*demand, {"model"}) )
        {
            appendOptionLine(out, "DEMAND_MODEL",
                requireToken(*demand, {"model"}, "options demand"));
        }
        if ( hasMember(*demand, {"multiplier"}) )
        {
            appendOptionLine(out, "DEMAND_MULTIPLIER",
                requireToken(*demand, {"multiplier"}, "options demand"));
        }
        if ( hasMember(*demand, {"pattern", "patternId"}) )
        {
            appendOptionLine(out, "DEMAND_PATTERN",
                requireToken(*demand, {"pattern", "patternId"}, "options demand"));
        }
    }

    const JsonValue* quality = findMember(sectionValue, {"quality"});
    if ( quality != nullptr )
    {
        if ( !quality->isObject() ) throw InputError(InputError::UNSPECIFIED,
            "JSON options quality section must be an object");
        string type = optionalToken(*quality, {"type"});
        string name = optionalToken(*quality, {"name"});
        string units = optionalToken(*quality, {"units"});
        string traceNode = optionalToken(*quality, {"traceNode", "trace"});
        if ( !type.empty() )
        {
            if ( Utilities::match(type, "TRACE") ) appendOptionLine(out, "QUALITY", type, traceNode);
            else if ( Utilities::match(type, "CHEMICAL") ) appendOptionLine(out, "QUALITY", name.empty() ? type : name, units);
            else if ( Utilities::match(type, "NONE") || Utilities::match(type, "AGE") ) appendOptionLine(out, "QUALITY", type);
            else appendOptionLine(out, "QUALITY", type, units);
        }
    }

    if ( hasMember(sectionValue, {"hydraulicSolver", "hydSolver"}) )
    {
        appendOptionLine(out, "HYD_SOLVER",
            requireToken(sectionValue, {"hydraulicSolver", "hydSolver"}, "options"));
    }
    if ( hasMember(sectionValue, {"matrixSolver"}) )
    {
        appendOptionLine(out, "MATRIX_SOLVER",
            requireToken(sectionValue, {"matrixSolver"}, "options"));
    }
    if ( hasMember(sectionValue, {"viscosity", "specificViscosity"}) )
    {
        appendOptionLine(out, "SPECIFIC_VISCOSITY",
            requireToken(sectionValue, {"viscosity", "specificViscosity"}, "options"));
    }
    if ( hasMember(sectionValue, {"specificGravity"}) )
    {
        appendOptionLine(out, "SPECIFIC_GRAVITY",
            requireToken(sectionValue, {"specificGravity"}, "options"));
    }
    if ( hasMember(sectionValue, {"specificDiffusivity"}) )
    {
        appendOptionLine(out, "SPECIFIC_DIFFUSIVITY",
            requireToken(sectionValue, {"specificDiffusivity"}, "options"));
    }
    if ( hasMember(sectionValue, {"emitterExponent"}) )
    {
        appendOptionLine(out, "EMITTER_EXPONENT",
            requireToken(sectionValue, {"emitterExponent"}, "options"));
    }
    if ( hasMember(sectionValue, {"qualityTolerance"}) )
    {
        appendOptionLine(out, "QUALITY_TOLERANCE",
            requireToken(sectionValue, {"qualityTolerance"}, "options"));
    }
    if ( hasMember(sectionValue, {"relativeAccuracy"}) )
    {
        appendOptionLine(out, "RELATIVE_ACCURACY",
            requireToken(sectionValue, {"relativeAccuracy"}, "options"));
    }
    if ( hasMember(sectionValue, {"headTolerance"}) )
    {
        appendOptionLine(out, "HEAD_TOLERANCE",
            requireToken(sectionValue, {"headTolerance"}, "options"));
    }
    if ( hasMember(sectionValue, {"flowTolerance"}) )
    {
        appendOptionLine(out, "FLOW_TOLERANCE",
            requireToken(sectionValue, {"flowTolerance"}, "options"));
    }
    if ( hasMember(sectionValue, {"flowChangeLimit"}) )
    {
        appendOptionLine(out, "FLOW_CHANGE_LIMIT",
            requireToken(sectionValue, {"flowChangeLimit"}, "options"));
    }
    if ( hasMember(sectionValue, {"timeWeight"}) )
    {
        appendOptionLine(out, "TIME_WEIGHT",
            requireToken(sectionValue, {"timeWeight"}, "options"));
    }
    if ( hasMember(sectionValue, {"trials", "maximumTrials"}) )
    {
        appendOptionLine(out, "MAXIMUM_TRIALS",
            requireToken(sectionValue, {"trials", "maximumTrials"}, "options"));
    }
    if ( hasMember(sectionValue, {"ifUnbalanced"}) )
    {
        appendOptionLine(out, "IF_UNBALANCED",
            requireToken(sectionValue, {"ifUnbalanced"}, "options"));
    }
    if ( hasMember(sectionValue, {"stepSizing"}) )
    {
        appendOptionLine(out, "STEP_SIZING",
            requireToken(sectionValue, {"stepSizing"}, "options"));
    }
    if ( hasMember(sectionValue, {"minimumPressure"}) )
    {
        appendOptionLine(out, "MINIMUM_PRESSURE",
            requireToken(sectionValue, {"minimumPressure"}, "options"));
    }
    if ( hasMember(sectionValue, {"servicePressure"}) )
    {
        appendOptionLine(out, "SERVICE_PRESSURE",
            requireToken(sectionValue, {"servicePressure"}, "options"));
    }
    if ( hasMember(sectionValue, {"pressureExponent"}) )
    {
        appendOptionLine(out, "PRESSURE_EXPONENT",
            requireToken(sectionValue, {"pressureExponent"}, "options"));
    }

    const JsonValue* leakage = findMember(sectionValue, {"leakage"});
    if ( leakage != nullptr )
    {
        if ( !leakage->isObject() ) throw InputError(InputError::UNSPECIFIED,
            "JSON options leakage section must be an object");
        if ( hasMember(*leakage, {"model"}) )
        {
            appendOptionLine(out, "LEAKAGE_MODEL",
                requireToken(*leakage, {"model"}, "options leakage"));
        }
        if ( hasMember(*leakage, {"coeff1"}) )
        {
            appendOptionLine(out, "LEAKAGE_COEFF1",
                requireToken(*leakage, {"coeff1"}, "options leakage"));
        }
        if ( hasMember(*leakage, {"coeff2"}) )
        {
            appendOptionLine(out, "LEAKAGE_COEFF2",
                requireToken(*leakage, {"coeff2"}, "options leakage"));
        }
    }
}

void appendTimesSection(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    if ( !sectionValue.isObject() )
    {
        throw InputError(InputError::UNSPECIFIED,
            "JSON times section must be an object or raw lines");
    }

    if ( hasMember(sectionValue, {"duration"}) ) appendOptionLine(out, "DURATION",
        requireToken(sectionValue, {"duration"}, "times"));
    if ( hasMember(sectionValue, {"hydraulicTimestep", "hydStep"}) ) appendOptionLine(out, "HYDRAULIC TIMESTEP",
        requireToken(sectionValue, {"hydraulicTimestep", "hydStep"}, "times"));
    if ( hasMember(sectionValue, {"qualityTimestep", "qualStep"}) ) appendOptionLine(out, "QUALITY TIMESTEP",
        requireToken(sectionValue, {"qualityTimestep", "qualStep"}, "times"));
    if ( hasMember(sectionValue, {"patternTimestep", "patternStep"}) ) appendOptionLine(out, "PATTERN TIMESTEP",
        requireToken(sectionValue, {"patternTimestep", "patternStep"}, "times"));
    if ( hasMember(sectionValue, {"patternStart"}) ) appendOptionLine(out, "PATTERN START",
        requireToken(sectionValue, {"patternStart"}, "times"));
    if ( hasMember(sectionValue, {"reportTimestep", "reportStep"}) ) appendOptionLine(out, "REPORT TIMESTEP",
        requireToken(sectionValue, {"reportTimestep", "reportStep"}, "times"));
    if ( hasMember(sectionValue, {"reportStart"}) ) appendOptionLine(out, "REPORT START",
        requireToken(sectionValue, {"reportStart"}, "times"));
    if ( hasMember(sectionValue, {"ruleTimestep", "ruleStep"}) ) appendOptionLine(out, "RULE TIMESTEP",
        requireToken(sectionValue, {"ruleTimestep", "ruleStep"}, "times"));
    if ( hasMember(sectionValue, {"startClocktime", "startTime"}) ) appendOptionLine(out, "START CLOCKTIME",
        requireToken(sectionValue, {"startClocktime", "startTime"}, "times"));
    if ( hasMember(sectionValue, {"statistic"}) ) appendOptionLine(out, "STATISTIC",
        requireToken(sectionValue, {"statistic"}, "times"));
}

void appendReportSection(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    if ( !sectionValue.isObject() )
    {
        throw InputError(InputError::UNSPECIFIED,
            "JSON report section must be an object or raw lines");
    }

    if ( hasMember(sectionValue, {"summary"}) ) appendOptionLine(out, "SUMMARY",
        scalarToToken(*findMember(sectionValue, {"summary"})));
    if ( hasMember(sectionValue, {"energy"}) ) appendOptionLine(out, "ENERGY",
        scalarToToken(*findMember(sectionValue, {"energy"})));
    if ( hasMember(sectionValue, {"status"}) ) appendOptionLine(out, "STATUS",
        scalarToToken(*findMember(sectionValue, {"status"})));
    if ( hasMember(sectionValue, {"trials"}) ) appendOptionLine(out, "TRIALS",
        scalarToToken(*findMember(sectionValue, {"trials"})));
    if ( hasMember(sectionValue, {"nodes"}) ) appendOptionLine(out, "NODES",
        requireToken(sectionValue, {"nodes"}, "report"));
    if ( hasMember(sectionValue, {"links"}) ) appendOptionLine(out, "LINKS",
        requireToken(sectionValue, {"links"}, "report"));
}

void appendCoordinates(ostringstream& out, const JsonValue& sectionValue)
{
    if ( tryAppendRawLines(out, sectionValue) ) return;
    vector<const JsonValue*> entries = collectEntries(sectionValue, "coordinates");
    for (const JsonValue* entry : entries)
    {
        string node = requireToken(*entry, {"node", "id", "name"}, "coordinate");
        string x = requireToken(*entry, {"x", "xCoord", "xCoordinate"}, "coordinate");
        string y = requireToken(*entry, {"y", "yCoord", "yCoordinate"}, "coordinate");
        out << node << ' ' << x << ' ' << y << "\n";
    }
}

void appendRawSection(ostringstream& out, const JsonValue& sectionValue, const string& sectionName)
{
    if ( !tryAppendRawLines(out, sectionValue) )
    {
        throw InputError(InputError::UNSPECIFIED,
            "JSON section " + sectionName + " currently supports raw lines only");
    }
}

const JsonValue* locateSectionsRoot(const JsonValue& root)
{
    const JsonValue* sections = findMember(root, {"sections", "input", "network"});
    if ( sections != nullptr && sections->isObject() ) return sections;
    return &root;
}

void appendSectionIfPresent(ostringstream& out,
                            const JsonValue& sections,
                            initializer_list<const char*> aliases,
                            const string& header,
                            void (*builder)(ostringstream&, const JsonValue&))
{
    const JsonValue* value = findMember(sections, aliases);
    if ( value == nullptr ) return;
    appendSimpleSectionHeader(out, header);
    builder(out, *value);
    out << "\n";
}

void trimGeneratedLine(string& line)
{
    static const string whitespace = " \t\n\r";

    size_t pos = line.find(";");
    if ( pos != string::npos ) line.erase(pos);

    pos = line.find_last_not_of(whitespace);
    if ( pos != string::npos ) line.erase(pos + 1);
    else line.clear();
}

template <typename Handler>
void processTextLines(const string& text, Handler handler)
{
    istringstream input(text);
    string line;
    while ( getline(input, line) )
    {
        trimGeneratedLine(line);
        if ( line.empty() ) continue;
        handler(line);
    }
}

template <typename Builder, typename Handler>
void processSectionIfPresent(const JsonValue& sections,
                             initializer_list<const char*> aliases,
                             Builder builder,
                             Handler handler)
{
    const JsonValue* value = findMember(sections, aliases);
    if ( value == nullptr ) return;

    ostringstream out;
    builder(out, *value);
    processTextLines(out.str(), handler);
}

bool fileNameLooksJson(const char* fileName)
{
    if ( fileName == nullptr ) return false;
    string name(fileName);
    size_t dot = name.find_last_of('.');
    if ( dot == string::npos ) return false;
    string ext = name.substr(dot + 1);
    transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch)
    {
        return (char)tolower(ch);
    });
    return ext == "json" || ext == "jsn";
}

char firstNonWhitespace(istream& stream)
{
    char ch = '\0';
    while ( stream.get(ch) )
    {
        if ( !isspace((unsigned char)ch) ) return ch;
    }
    return '\0';
}

} // namespace

bool JsonInputReader::isJsonInput(const char* fileName, istream& stream)
{
    stream.clear();
    stream.seekg(0, ios::beg);
    char ch = firstNonWhitespace(stream);
    stream.clear();
    stream.seekg(0, ios::beg);

    if ( ch == '{' ) return true;
    return fileNameLooksJson(fileName);
}

void JsonInputReader::readFile(istream& stream, Network* network)
{
    stream.clear();
    stream.seekg(0, ios::beg);

    ostringstream buffer;
    buffer << stream.rdbuf();
    string jsonText = buffer.str();
    JsonParser parser(jsonText);
    JsonValue root = parser.parse();
    if ( !root.isObject() )
    {
        throw InputError(InputError::UNSPECIFIED,
            "JSON input must contain a root object");
    }

    const JsonValue* sections = locateSectionsRoot(root);

    ObjectParser objectParser(network);
    processSectionIfPresent(*sections, {"junctions", "junction"}, appendJunctions,
        [&](string& line)
        {
            objectParser.parseLine(line, InputReader::JUNCTION);
        });
    processSectionIfPresent(*sections, {"reservoirs", "reservoir"}, appendReservoirs,
        [&](string& line)
        {
            objectParser.parseLine(line, InputReader::RESERVOIR);
        });
    processSectionIfPresent(*sections, {"tanks", "tank"}, appendTanks,
        [&](string& line)
        {
            objectParser.parseLine(line, InputReader::TANK);
        });
    processSectionIfPresent(*sections, {"pipes", "pipe"}, appendPipes,
        [&](string& line)
        {
            objectParser.parseLine(line, InputReader::PIPE);
        });
    processSectionIfPresent(*sections, {"pumps", "pump"}, appendPumps,
        [&](string& line)
        {
            objectParser.parseLine(line, InputReader::PUMP);
        });
    processSectionIfPresent(*sections, {"valves", "valve"}, appendValves,
        [&](string& line)
        {
            objectParser.parseLine(line, InputReader::VALVE);
        });
    processSectionIfPresent(*sections, {"patterns", "pattern"}, appendPatterns,
        [&](string& line)
        {
            objectParser.parseLine(line, InputReader::PATTERN);
        });
    processSectionIfPresent(*sections, {"curves", "curve"}, appendCurves,
        [&](string& line)
        {
            objectParser.parseLine(line, InputReader::CURVE);
        });

    PropertyParser propertyParser(network);
    processSectionIfPresent(*sections, {"title"}, appendTitleSection,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::TITLE);
        });
    processSectionIfPresent(*sections, {"junctions", "junction"}, appendJunctions,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::JUNCTION);
        });
    processSectionIfPresent(*sections, {"reservoirs", "reservoir"}, appendReservoirs,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::RESERVOIR);
        });
    processSectionIfPresent(*sections, {"tanks", "tank"}, appendTanks,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::TANK);
        });
    processSectionIfPresent(*sections, {"pipes", "pipe"}, appendPipes,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::PIPE);
        });
    processSectionIfPresent(*sections, {"pumps", "pump"}, appendPumps,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::PUMP);
        });
    processSectionIfPresent(*sections, {"valves", "valve"}, appendValves,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::VALVE);
        });
    processSectionIfPresent(*sections, {"patterns", "pattern"}, appendPatterns,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::PATTERN);
        });
    processSectionIfPresent(*sections, {"curves", "curve"}, appendCurves,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::CURVE);
        });
    processSectionIfPresent(*sections, {"controls", "control"}, appendControls,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::CONTROL);
        });
    processSectionIfPresent(*sections, {"emitters", "emitter"}, appendEmitters,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::EMITTER);
        });
    processSectionIfPresent(*sections, {"demands", "demand"}, appendDemands,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::DEMAND);
        });
    processSectionIfPresent(*sections, {"status"}, appendStatus,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::STATUS);
        });
    processSectionIfPresent(*sections, {"leakage"}, appendLeakage,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::LEAKAGE);
        });
    processSectionIfPresent(*sections, {"energy"}, appendEnergy,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::ENERGY);
        });
    processSectionIfPresent(*sections, {"quality"}, appendQuality,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::QUALITY);
        });
    processSectionIfPresent(*sections, {"sources", "source"}, appendSources,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::SOURCE);
        });
    processSectionIfPresent(*sections, {"reactions", "reaction"}, appendReactions,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::REACTION);
        });
    processSectionIfPresent(*sections, {"mixing"}, appendMixing,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::MIXING);
        });
    processSectionIfPresent(*sections, {"options", "option"}, appendOptionsSection,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::OPTION);
        });
    processSectionIfPresent(*sections, {"times", "time"}, appendTimesSection,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::TIME);
        });
    processSectionIfPresent(*sections, {"report"}, appendReportSection,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::REPORT);
        });
    processSectionIfPresent(*sections, {"coordinates", "coords", "coord"}, appendCoordinates,
        [&](string& line)
        {
            propertyParser.parseLine(line, InputReader::COORD);
        });
}
