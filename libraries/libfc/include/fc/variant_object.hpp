#pragma once
#include <fc/variant.hpp>
#include <memory>

namespace fc
{
   class mutable_variant_object;

   /**
    *  @ingroup Serializable
    *
    *  @brief An order-preserving dictionary of variants.
    *
    *  Keys are kept in the order they are inserted.
    *  This dictionary implements copy-on-write
    *
    *  @note This class is not optimized for random-access on large
    *        sets of key-value pairs.
    */
   class variant_object
   {
   public:
      /** @brief a key/value pair */
      class entry
      {
      public:
         entry();
         entry( std::string k, variant v );
         entry( entry&& e );
         entry( const entry& e);
         entry& operator=(const entry&);
         entry& operator=(entry&&);

         const std::string& key()const;
         const variant& value()const;
         void  set( variant v );

         variant&       value();

         friend bool operator == (const entry& a, const entry& b) {
            return a._key == b._key && a._value == b._value;
         }
         friend bool operator != (const entry& a, const entry& b) {
            return !(a == b);
         }

      private:
         std::string _key;
         variant     _value;
      };

      typedef std::vector< entry >::const_iterator iterator;

      /**
         * @name Immutable Interface
         *
         * Calling these methods will not result in copies of the
         * underlying type.
         */
      ///@{
      iterator begin()const;
      iterator end()const;
      iterator find( const std::string& key )const;
      iterator find( const char* key )const;
      const variant& operator[]( const std::string& key )const;
      const variant& operator[]( const char* key )const;
      size_t size()const;
      bool   contains( const char* key ) const { return find(key) != end(); }
      ///@}

      variant_object();

      /** initializes the first key/value pair in the object */
      variant_object( std::string key, variant val );

      template<typename T>
      variant_object( std::string key, T&& val )
      :_key_value( std::make_shared<std::vector<entry> >() )
      {
         *this = variant_object( std::move(key), variant(std::forward<T>(val)) );
      }
      variant_object( const variant_object& );
      variant_object( variant_object&& );

      variant_object( const mutable_variant_object& );
      variant_object( mutable_variant_object&& );

      variant_object& operator=( variant_object&& );
      variant_object& operator=( const variant_object& );

      variant_object& operator=( mutable_variant_object&& );
      variant_object& operator=( const mutable_variant_object& );

      size_t estimated_size()const;

   private:
      std::shared_ptr< std::vector< entry > > _key_value;
      friend class mutable_variant_object;
   };
   /** @ingroup Serializable */
   void to_variant( const variant_object& var,  variant& vo );
   /** @ingroup Serializable */
   void from_variant( const variant& var,  variant_object& vo );


  /**
   *  @ingroup Serializable
   *
   *  @brief An order-preserving dictionary of variants.
   *
   *  Keys are kept in the order they are inserted.
   *  This dictionary implements copy-on-write
   *
   *  @note This class is not optimized for random-access on large
   *        sets of key-value pairs.
   */
   class mutable_variant_object
   {
   public:
      /** @brief a key/value pair */
      typedef variant_object::entry  entry;

      typedef std::vector< entry >::iterator       iterator;
      typedef std::vector< entry >::const_iterator const_iterator;

      /**
         * @name Immutable Interface
         *
         * Calling these methods will not result in copies of the
         * underlying type.
         */
      ///@{
      iterator begin()const;
      iterator end()const;
      iterator find( const std::string& key )const;
      iterator find( const char* key )const;
      const variant& operator[]( const std::string& key )const;
      const variant& operator[]( const char* key )const;
      size_t size()const;
      ///@}
      variant& operator[]( const std::string& key );
      variant& operator[]( const char* key );

      /**
         * @name mutable Interface
         *
         * Calling these methods will result in a copy of the underlying type
         * being created if there is more than one reference to this object.
         */
      ///@{
      void                 reserve( size_t s);
      iterator             begin();
      iterator             end();
      void                 erase( const std::string& key );
      /**
         *
         * @return end() if key is not found
         */
      iterator             find( const std::string& key );
      iterator             find( const char* key );


      /** replaces the value at \a key with \a var or inserts \a key if not found */
      mutable_variant_object& set( std::string key, variant var ) &;
      mutable_variant_object set( std::string key, variant var ) &&;

     /** Appends \a key and \a var without checking for duplicates, designed to
         *  simplify construction of dictionaries using (key,val)(key2,val2) syntax
         */
      /**
      *  Convenience method to simplify the manual construction of
      *  variant_objects
      *
      *  Instead of:
      *    <code>mutable_variant_object("c",c).set("a",a).set("b",b);</code>
      *
      *  You can use:
      *    <code>mutable_variant_object( "c", c )( "b", b)( "c",c )</code>
      *
      *  @return *this;
      */
      mutable_variant_object& operator()( std::string key, variant var ) &;
      mutable_variant_object operator()( std::string key, variant var ) &&;
      template<typename T>
      mutable_variant_object& operator()( std::string key, T&& var ) &
      {
         set(std::move(key), variant( fc::forward<T>(var) ) );
         return *this;
      }
      template<typename T>
      mutable_variant_object operator()( std::string key, T&& var ) &&
      {
         set(std::move(key), variant( fc::forward<T>(var) ) );
         return std::move(*this);
      }
      /**
       * Copy a variant_object into this mutable_variant_object.
       */
      mutable_variant_object& operator()( const variant_object& vo ) &;
      mutable_variant_object operator()( const variant_object& vo ) &&;
      /**
       * Copy another mutable_variant_object into this mutable_variant_object.
       */
      mutable_variant_object& operator()( const mutable_variant_object& mvo ) &;
      mutable_variant_object operator()( const mutable_variant_object& mvo ) &&;
      ///@}


      template<typename T,
               typename = std::enable_if_t<!std::is_base_of<mutable_variant_object,
                                                            std::decay_t<T>>::value>>
      explicit mutable_variant_object( T&& v )
      :_key_value( new std::vector<entry>() )
      {
          *this = variant(fc::forward<T>(v)).get_object();
      }

      mutable_variant_object();

      /** initializes the first key/value pair in the object */
      mutable_variant_object( std::string key, variant val );
      template<typename T>
      mutable_variant_object( std::string key, T&& val )
      :_key_value( new std::vector<entry>() )
      {
         set( std::move(key), variant(std::forward<T>(val)) );
      }

      mutable_variant_object( mutable_variant_object&& );
      mutable_variant_object( const mutable_variant_object& );
      mutable_variant_object( const variant_object& );

      mutable_variant_object& operator=( mutable_variant_object&& );
      mutable_variant_object& operator=( const mutable_variant_object& );
      mutable_variant_object& operator=( const variant_object& );
   private:
      std::unique_ptr< std::vector< entry > > _key_value;
      friend class variant_object;
   };
   /** @ingroup Serializable */
   void to_variant( const mutable_variant_object& var,  variant& vo );
   /** @ingroup Serializable */
   void from_variant( const variant& var,  mutable_variant_object& vo );

} // namespace fc
