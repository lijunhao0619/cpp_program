#pragma once

#include <boost/endian/conversion.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace zk_client {

// ============================================================
// Wire protocol: all integers are big-endian on the wire
// ============================================================

namespace detail {

inline void write_int32(std::vector<char>& buf, int32_t v) {
    auto be = boost::endian::native_to_big(v);
    buf.insert(buf.end(), reinterpret_cast<const char*>(&be),
               reinterpret_cast<const char*>(&be) + 4);
}

inline void write_int64(std::vector<char>& buf, int64_t v) {
    auto be = boost::endian::native_to_big(v);
    buf.insert(buf.end(), reinterpret_cast<const char*>(&be),
               reinterpret_cast<const char*>(&be) + 8);
}

inline void write_bool(std::vector<char>& buf, bool v) {
    buf.push_back(v ? '\1' : '\0');
}

inline void write_string(std::vector<char>& buf, const std::string& s) {
    write_int32(buf, static_cast<int32_t>(s.size()));
    buf.insert(buf.end(), s.data(), s.data() + s.size());
}

inline void write_buffer(std::vector<char>& buf, const std::vector<char>& data) {
    write_int32(buf, static_cast<int32_t>(data.size()));
    buf.insert(buf.end(), data.begin(), data.end());
}

inline int32_t read_int32(const char*& p) {
    int32_t be;
    std::memcpy(&be, p, 4);
    p += 4;
    return boost::endian::big_to_native(be);
}

inline int64_t read_int64(const char*& p) {
    int64_t be;
    std::memcpy(&be, p, 8);
    p += 8;
    return boost::endian::big_to_native(be);
}

inline bool read_bool(const char*& p) {
    return *p++ != 0;
}

inline std::string read_string(const char*& p) {
    int32_t len = read_int32(p);
    std::string s(p, len);
    p += len;
    return s;
}

inline std::vector<char> read_buffer(const char*& p) {
    int32_t len = read_int32(p);
    std::vector<char> v(p, p + len);
    p += len;
    return v;
}

} // namespace detail

// ============================================================
// ZK Op Codes
// ============================================================

enum class op_code : int32_t {
    auth = 100,
    create = 1,
    remove = 2,
    exists = 3,
    get_data = 4,
    set_data = 5,
    get_acl = 6,
    set_acl = 7,
    get_children = 8,
    sync = 9,
    ping = 11,
    close_session = -11,
};

// ============================================================
// ZK Stat structure
// ============================================================

struct zk_stat {
    int64_t czxid = 0;
    int64_t mzxid = 0;
    int64_t ctime = 0;
    int64_t mtime = 0;
    int32_t version = 0;
    int32_t cversion = 0;
    int32_t aversion = 0;
    int64_t ephemeral_owner = 0;
    int32_t data_length = 0;
    int32_t num_children = 0;
    int64_t pzxid = 0;

    static zk_stat deserialize(const char*& p) {
        zk_stat st;
        st.czxid = detail::read_int64(p);
        st.mzxid = detail::read_int64(p);
        st.ctime = detail::read_int64(p);
        st.mtime = detail::read_int64(p);
        st.version = detail::read_int32(p);
        st.cversion = detail::read_int32(p);
        st.aversion = detail::read_int32(p);
        st.ephemeral_owner = detail::read_int64(p);
        st.data_length = detail::read_int32(p);
        st.num_children = detail::read_int32(p);
        st.pzxid = detail::read_int64(p);
        return st;
    }
};

// ============================================================
// ACL
// ============================================================

struct zk_acl {
    enum class permission : int32_t {
        none = 0,
        read = 0b00001,
        write = 0b00010,
        create = 0b00100,
        _delete = 0b01000,
        admin = 0b10000,
        all = 0b11111,
    };

    permission perms = permission::all;
    std::string scheme = "world";
    std::string id = "anyone";

    void serialize(std::vector<char>& buf) const {
        detail::write_int32(buf, static_cast<int32_t>(perms));
        detail::write_string(buf, scheme);
        detail::write_string(buf, id);
    }
};

inline std::vector<zk_acl> acl_open_unsafe() {
    return {{zk_acl::permission::all, "world", "anyone"}};
}

// ============================================================
// Request header
// ============================================================

struct request_header {
    int32_t xid;
    int32_t opcode;

    void serialize(std::vector<char>& buf) const {
        detail::write_int32(buf, xid);
        detail::write_int32(buf, opcode);
    }
};

// ============================================================
// Reply header
// ============================================================

struct reply_header {
    int32_t xid;
    int64_t zxid;
    int32_t err;

    static reply_header deserialize(const char*& p) {
        reply_header rh;
        rh.xid = detail::read_int32(p);
        rh.zxid = detail::read_int64(p);
        rh.err = detail::read_int32(p);
        return rh;
    }
};

// ============================================================
// Connect
// ============================================================

struct connect_request {
    int32_t protocol_version = 0;
    int64_t last_zxid_seen = 0;
    int32_t timeout_ms = 4000;
    int64_t session_id = 0;
    std::vector<char> passwd = std::vector<char>(16, '\0');
    bool read_only = false;

    std::vector<char> serialize() const {
        std::vector<char> buf;
        detail::write_int32(buf, protocol_version);
        detail::write_int64(buf, last_zxid_seen);
        detail::write_int32(buf, timeout_ms);
        detail::write_int64(buf, session_id);
        detail::write_buffer(buf, passwd);
        detail::write_bool(buf, read_only);
        return buf;
    }
};

struct connect_response {
    int32_t protocol_version;
    int32_t timeout_ms;
    int64_t session_id;
    std::vector<char> passwd;
    bool read_only;

    static connect_response deserialize(const char*& p) {
        connect_response r;
        r.protocol_version = detail::read_int32(p);
        r.timeout_ms = detail::read_int32(p);
        r.session_id = detail::read_int64(p);
        r.passwd = detail::read_buffer(p);
        r.read_only = detail::read_bool(p);
        return r;
    }
};

// ============================================================
// Create
// ============================================================

struct create_request {
    std::string path;
    std::vector<char> data;
    std::vector<zk_acl> acls;
    int32_t flags = 0; // 0=persistent, 1=ephemeral, 2=persistent_sequential, 3=ephemeral_sequential

    std::vector<char> serialize_body() const {
        std::vector<char> buf;
        detail::write_string(buf, path);
        detail::write_buffer(buf, data);
        detail::write_int32(buf, static_cast<int32_t>(acls.size()));
        for (auto& a : acls) a.serialize(buf);
        detail::write_int32(buf, flags);
        return buf;
    }
};

struct create_response {
    std::string path;

    static create_response deserialize(const char*& p) {
        return {detail::read_string(p)};
    }
};

// ============================================================
// Get Children
// ============================================================

struct get_children_request {
    std::string path;
    bool watch = false;

    std::vector<char> serialize_body() const {
        std::vector<char> buf;
        detail::write_string(buf, path);
        detail::write_bool(buf, watch);
        return buf;
    }
};

struct get_children_response {
    std::vector<std::string> children;

    static get_children_response deserialize(const char*& p) {
        get_children_response r;
        int32_t count = detail::read_int32(p);
        for (int32_t i = 0; i < count; ++i)
            r.children.push_back(detail::read_string(p));
        return r;
    }
};

// ============================================================
// Get Data
// ============================================================

struct get_data_request {
    std::string path;
    bool watch = false;

    std::vector<char> serialize_body() const {
        std::vector<char> buf;
        detail::write_string(buf, path);
        detail::write_bool(buf, watch);
        return buf;
    }
};

struct get_data_response {
    std::vector<char> data;
    zk_stat stat;

    static get_data_response deserialize(const char*& p) {
        get_data_response r;
        r.data = detail::read_buffer(p);
        r.stat = zk_stat::deserialize(p);
        return r;
    }
};

// ============================================================
// Exists
// ============================================================

struct exists_request {
    std::string path;
    bool watch = false;

    std::vector<char> serialize_body() const {
        std::vector<char> buf;
        detail::write_string(buf, path);
        detail::write_bool(buf, watch);
        return buf;
    }
};

struct exists_response {
    zk_stat stat;

    static exists_response deserialize(const char*& p) {
        exists_response r;
        r.stat = zk_stat::deserialize(p);
        return r;
    }
};

// ============================================================
// Remove
// ============================================================

struct delete_request {
    std::string path;
    int32_t version = -1; // -1 means any version

    std::vector<char> serialize_body() const {
        std::vector<char> buf;
        detail::write_string(buf, path);
        detail::write_int32(buf, version);
        return buf;
    }
};

// empty response, nothing to deserialize

// ============================================================
// Common: wrap body with 4-byte length prefix
// ============================================================

inline std::vector<char> make_zpayload(const std::vector<char>& body) {
    std::vector<char> out;
    detail::write_int32(out, static_cast<int32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

// ============================================================
// Error codes
// ============================================================

inline const char* zk_error_str(int err) {
    switch (err) {
        case 0: return "ok";
        case -1: return "system error";
        case -2: return "runtime inconsistency";
        case -3: return "data inconsistency";
        case -4: return "connection loss";
        case -5: return "marshalling error";
        case -6: return "unimplemented";
        case -7: return "operation timeout";
        case -8: return "bad arguments";
        case -100: return "api error";
        case -101: return "no node";
        case -102: return "no auth";
        case -103: return "bad version";
        case -108: return "no children for ephemerals";
        case -110: return "node exists";
        case -111: return "not empty";
        default: return "unknown error";
    }
}

} // namespace zk_client
