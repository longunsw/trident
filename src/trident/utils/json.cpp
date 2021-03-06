#include <trident/utils/json.h>

#include <rapidjson/document.h>

std::string escape_json(const std::string &s) {
    std::ostringstream o;
    for (auto c = s.cbegin(); c != s.cend(); c++) {
        if (*c == '"' || *c == '\\' || ('\x00' <= *c && *c <= '\x1f')) {
            o << "\\u"
                << std::hex << std::setw(4) << std::setfill('0') << (int)*c;
        } else {
            o << *c;
        }
    }
    return o.str();
}

void JSON::write(std::ostream &out, JSON &value) {
    if (value.listchildren.size() > 0) {
        if (!value.listvalues.empty() ||
                !value.values.empty() ||
                !value.children.empty()) {
            throw 10;
        }
        out << "[";
        bool firstel = true;
        for(auto v : value.listchildren) {
            if (!firstel)
                out << ",";
            write(out, v);
            firstel = false;
        }
        out << "]";
    } else if (value.listvalues.size() > 0) {
        if (!value.values.empty() ||
                !value.children.empty()) {
            throw 10;
        }
        out << "[";
        bool firstel = true;
        for(auto v : value.listvalues) {
            if (!firstel)
                out << ",";
            out << "\"" << escape_json(v) << "\"";
            firstel = false;
        }
        out << "]";
    } else {
        out << "{";
        bool first = true;
        for (auto v : value.values) {
            if (!first)
                out << ",";
            out << "\"" << escape_json(v.first) << "\" : \"" << escape_json(v.second) << "\"";
            first = false;
        }
        for (auto v : value.children) {
            if (!first)
                out << ",";
            out << "\"" << escape_json(v.first) << "\" : ";
            write(out, v.second);
            first = false;
        }
        out << "}";
    }
}

void parse_array(const rapidjson::Value &value,
        std::vector<JSON> &out1,
        std::vector<std::string> &out2,
        bool &jsonArray) {
}

void parse_json(const rapidjson::Value &src, JSON &dest) {
    if (src.IsObject()) {
        for (rapidjson::Value::ConstMemberIterator itr = src.MemberBegin();
                itr != src.MemberEnd(); ++itr) {
            std::string name = itr->name.GetString();
            //Could be JSON or string-like element
            if (itr->value.IsObject()) {
                JSON child;
                parse_json(itr->value, child);
                dest.add_child(name, child);
            } else if (itr->value.IsArray()) {
                bool isArrayOfObjects = false;
                std::vector<JSON> c1;
                std::vector<std::string> c2;
                parse_array(itr->value, c1, c2, isArrayOfObjects);
                JSON cvalue;
                if (isArrayOfObjects) {
                    for(auto &c : c1) {
                        cvalue.push_back(c);
                    }
                } else {
                    for(auto &c : c2) {
                        cvalue.push_back(c);
                    }
                }
                dest.add_child(name, cvalue);
            } else {
                dest.put(name, itr->value.GetString());
            }

        }
    } else if (src.IsArray()) { //array
        bool isArrayOfObjects = false;
        std::vector<JSON> c1;
        std::vector<std::string> c2;
        parse_array(src, c1, c2, isArrayOfObjects);
        if (isArrayOfObjects) {
            for(auto &c : c1) {
                dest.push_back(c);
            }
        } else {
            for(auto &c : c2) {
                dest.push_back(c);
            }
        }
    } else {
        LOG(ERRORL) << "Not supported";
        throw 10;
    }
}

void JSON::read(std::string &in, JSON &value) {
    rapidjson::Document jsonObj;
    const char *t = in.c_str();
    if (jsonObj.Parse<0>(t).HasParseError()) {
        LOG(ERRORL) << "Errors in parsing the JSON string " << in;
        throw 10;
    }
    parse_json(jsonObj, value);

}
