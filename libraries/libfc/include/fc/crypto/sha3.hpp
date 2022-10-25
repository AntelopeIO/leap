#pragma once
#include <fc/fwd.hpp>
#include <fc/string.hpp>
#include <fc/platform_independence.hpp>
#include <fc/io/raw_fwd.hpp>
#include <boost/functional/hash.hpp>

namespace fc
{

class sha3
{
public:
	sha3();
	~sha3(){}
	explicit sha3(const string &hex_str);
	explicit sha3(const char *data, size_t size);

	string str() const;
	operator string() const;

	const char *data() const;
	char* data();
	size_t data_size() const { return 256 / 8; }

	static sha3 hash(const char *d, uint32_t dlen, bool is_nist=true) {
		encoder e;
		e.write(d, dlen);
		const auto& sha = e.result(is_nist);
		return sha;
	}
	static sha3 hash(const string& s, bool is_nist=true) { return hash(s.c_str(), s.size(), is_nist); }
	static sha3 hash(const sha3& s, bool is_nist=true) { return hash(s.data(), sizeof(s._hash), is_nist); }

	template <typename T>
	static sha3 hash(const T &t, bool is_nist=true)
	{
		sha3::encoder e;
		fc::raw::pack(e, t);
		return e.result(is_nist);
	}

	class encoder
	{
	public:
		encoder();
		~encoder();

		void write(const char *d, uint32_t dlen);
		void put(char c) { write(&c, 1); }
		void reset();
		sha3 result(bool is_nist=true);

	private:
		struct impl;
		fc::fwd<impl, 1016> my;
	};

	template <typename T>
	inline friend T &operator<<(T &ds, const sha3 &ep)
	{
		ds.write(ep.data(), sizeof(ep));
		return ds;
	}

	template <typename T>
	inline friend T &operator>>(T &ds, sha3 &ep)
	{
		ds.read(ep.data(), sizeof(ep));
		return ds;
	}
	friend sha3 operator<<(const sha3 &h1, uint32_t i);
	friend sha3 operator>>(const sha3 &h1, uint32_t i);
	friend bool operator==(const sha3 &h1, const sha3 &h2);
	friend bool operator!=(const sha3 &h1, const sha3 &h2);
	friend sha3 operator^(const sha3 &h1, const sha3 &h2);
	friend bool operator>=(const sha3 &h1, const sha3 &h2);
	friend bool operator>(const sha3 &h1, const sha3 &h2);
	friend bool operator<(const sha3 &h1, const sha3 &h2);

	uint64_t _hash[4];
};

class variant;
void to_variant(const sha3 &bi, variant &v);
void from_variant(const variant &v, sha3 &bi);

} // namespace fc

namespace std
{
template <>
struct hash<fc::sha3>
{
	size_t operator()(const fc::sha3 &s) const
	{
		return *((size_t *)&s);
	}
};

} // namespace std

namespace boost
{
template <>
struct hash<fc::sha3>
{
	size_t operator()(const fc::sha3 &s) const
	{
		return s._hash[3]; //*((size_t*)&s);
	}
};
} // namespace boost
#include <fc/reflect/reflect.hpp>
FC_REFLECT_TYPENAME(fc::sha3)
