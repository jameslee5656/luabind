// Copyright (c) 2003 Daniel Wallin and Arvid Norberg

// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
// ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
// TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
// SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
// ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.


#ifndef LUABIND_CLASS_HPP_INCLUDED
#define LUABIND_CLASS_HPP_INCLUDED

/*
	Issues:
	-------
	* done *
	move all exceptions to the exception.hpp header

	* done *
	Since functions can be overridden, the constness of the member functions
	must be taken into account.

	* solved for member functions, not application operator *
	if we have a base class that defines a function a derived class must be able to
	override that function (not just overload). Right now we just add the other overload
	to the overloads list and will probably get an ambiguity. If we want to support this
	each method_rep must include a vector of type_info pointers for each parameter.
	Operators do not have this problem, since operators always have to have
	it's own type as one of the arguments, no ambiguity can occur. Application
	operator, on the other hand, would have this problem.
	Properties cannot be overloaded, so they should always be overridden.
	If this is to work for application operator, we really need to specify if an application
	operator is const or not.

	If one class registers two functions with the same name and the same
	signature, there's currently no error. The last registered function will
	be the one that's used.
	How do we know which class registered the function? If the function was
	defined by the base class, it is a legal operation, to override it.
	we cannot look at the pointer offset, since it always will be zero for one of the bases.

	* done *
	We do not currently run unwrap_other<> on the parameters given to the application
	operators when we register them.

	* done *
	if the first matched binary operator does not match the parameters we have to manually
	check if the right parameter has a matching operator.

	* I think this is resolved *
	There is currently a restriction that operators must take their parameters as const references
	or as copies. This could be solved by specifying special wrapper types for const references and
	non-const references. Binary operators shouldn't implicitly convert it's parameters to const&.

	* done *
	The application operator do not currently work on const objects. Again, to solve this we need
	some kind of wrapper to say that the self object is const.
	
	Make sure we don't have any memory leaks when we use lua_error()

	We should avoid names that begins with lua_ (in our own scopes) since the lua distribution
	have plenty of defines with that prefix.

	Optimize by calling lua_rawset/get instead of lua_settable/gettable

	* done *
	use ordinary function pointers instead of boost::function.
	the boost::function is only used on member functions now, We have to use it in order
	to support free functions as member functions.

	* solved *
	when we register classes we should import all methods and properties
	from the base classes into the class. We will then need to offset the object pointer
	on a per method/property basis. This will allow methods from the base class to be
	overloaded without shadowing the base class. The method lookup will also be
	more efficient.

	remove methods from methodtable of classrep when overloading them in lua

	* done *
	we currently set uninitialized references to -1, they should be set to LUA_NOREF!

	* done *
	the error exception class should avoid heap allocations

	Todo:
	-----

	document custom policies, custom converters

	store the instance object for policies.

	support the __concat metamethod. This is a bit tricky, since it cannot be
	treated as a normal operator. It is a binary operator but we want to use the
	__tostring implementation for both arguments.

	smart pointer support

	scopes

	__finalize function on lua classes

	make all proxy_callers one policy based class

	make all proxy_objects one policy based class

	static functions

*/

#include <luabind/config.hpp>

#include <string>
#include <map>
#include <vector>
#include <cassert>

#include <boost/static_assert.hpp>
#include <boost/type_traits.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/repetition/enum_params_with_a_default.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/mpl/list.hpp>
#include <boost/mpl/apply.hpp>
#include <boost/mpl/lambda.hpp>
#include <boost/mpl/logical.hpp>
#include <boost/mpl/find_if.hpp>
#include <boost/mpl/apply_if.hpp>
#include <boost/mpl/logical.hpp>

#include <luabind/config.hpp>
#include <luabind/detail/constructor.hpp>
#include <luabind/detail/call.hpp>
#include <luabind/detail/signature_match.hpp>
#include <luabind/detail/primitives.hpp>
#include <luabind/detail/property.hpp>
#include <luabind/detail/typetraits.hpp>
#include <luabind/detail/class_rep.hpp>
#include <luabind/detail/method_rep.hpp>
#include <luabind/detail/construct_rep.hpp>
#include <luabind/detail/object_rep.hpp>
#include <luabind/detail/operators.hpp>
#include <luabind/detail/call_member.hpp>
#include <luabind/detail/enum_maker.hpp>
#include <luabind/detail/get_signature.hpp>
#include <luabind/detail/implicit_cast.hpp>
#include <luabind/detail/calc_arity.hpp>

namespace luabind
{
	namespace detail
	{
		struct unspecified {};
	}

	template<class T, class X1 = detail::unspecified, class X2 = detail::unspecified, class X3 = detail::unspecified>
	struct class_;

	namespace detail
	{
		template<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_BASES, class A)>
		double is_bases_helper(const bases<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_BASES, A)>&);

#ifndef BOOST_MSVC
		template<class T>
		char is_bases_helper(const T&);
#else
		char is_bases_helper(...);
#endif

		template<class T>
		struct is_bases
		{
			static const T& t;

			BOOST_STATIC_CONSTANT(bool, value = sizeof(is_bases_helper(t)) == sizeof(double));
			typedef boost::mpl::bool_<value> type;
			BOOST_MPL_AUX_LAMBDA_SUPPORT(1,is_bases,(T))
		};

		double is_not_unspecified_helper(const unspecified*);
		char is_not_unspecified_helper(...);

		template<class T>
		struct is_not_unspecified
		{
			BOOST_STATIC_CONSTANT(bool, value = sizeof(is_not_unspecified_helper(static_cast<T*>(0))) == sizeof(char));
			typedef boost::mpl::bool_<value> type;
			BOOST_MPL_AUX_LAMBDA_SUPPORT(1,is_not_unspecified,(T))
		};

		template<class Predicate>
		struct get_predicate
		{
			typedef typename boost::mpl::and_<
						Predicate
					,	is_not_unspecified<boost::mpl::_1>
					> type;
		};

		template<class Parameters, class Predicate, class DefaultValue>
		struct extract_parameter
		{
			typedef typename get_predicate<Predicate>::type pred;
			typedef typename boost::mpl::find_if<Parameters, pred>::type iterator;
			typedef typename boost::mpl::apply_if<boost::is_same<iterator, typename boost::mpl::end<Parameters>::type>
				, boost::mpl::identity<DefaultValue>
				, iterator
			>::type type;
		};

		int function_dispatcher(lua_State* L);

		// this should know about the smart pointer type.
		// and should really do:
		// get_pointer(*static_cast<SmartPointer*>(obj_ptr))
		// to extract the held pointer.
		template<class HeldType, class T, class F, class Policies>
		struct function_callback_non_null
		{
			function_callback_non_null(F f_): f(f_) {}
			inline int operator()(lua_State* L, void* obj_ptr)
			{
				HeldType& held_obj = *static_cast<HeldType*>(obj_ptr);

				T* ptr = get_pointer(held_obj);
//				BOOST_STATIC_ASSERT((boost::is_same<HeldType, detail::null_type>::value == false));

//				std::cout << "HeldType: " << typeid(HeldType).name() << "\n";

				return call(f, ptr, L, static_cast<Policies*>(this));
			}
			F f;
		};

		template<class T, class F, class Policies>
		struct function_callback_null_type : Policies
		{
			function_callback_null_type(F f_): f(f_) {}
			inline int operator()(lua_State* L, void* obj_ptr)
			{
//				std::cout << "HeldType: null_type\n";
				T* ptr = static_cast<T*>(obj_ptr);
				return call(f, ptr, L, static_cast<Policies*>(this));
			}
			F f;
		};

		template<class HeldType, class T, class F, class Policies>
		struct function_callback_s
		{
			typedef typename
				boost::mpl::if_<boost::is_same<HeldType,detail::null_type>
					, function_callback_null_type<T,F,Policies>
					, function_callback_non_null<HeldType,T,F,Policies>
			>::type type;
		};

		template<class T, class F, class Policies>
		struct match_function_callback_s
		{
			static inline int apply(lua_State* L)
			{
				object_rep* obj = static_cast<object_rep*>(lua_touserdata(L, 1));
				F fptr = 0;
				return match(fptr, L, obj->flags() & object_rep::constant, static_cast<Policies*>(0));
			}
		};

		// prints the types of the values on the stack, in the
		// range [start_index, lua_gettop()]
		inline std::string stack_content_by_name(lua_State* L, int start_index)
		{
			std::string ret;
			int top = lua_gettop(L);
			for (int i = start_index; i <= top; ++i)
			{
				object_rep* obj = is_class_object(L, i);
				class_rep* crep = is_class_rep(L, i)?(class_rep*)lua_touserdata(L, i):0;
				if (obj == 0 && crep == 0)
				{
					int type = lua_type(L, i);
					ret += lua_typename(L, type);
				}
				else if (obj)
				{
					if (obj->flags() & object_rep::constant) ret += "const ";
					ret += obj->crep()->name();
				}
				else if (crep)
				{
					ret += "<";
					ret += crep->name();
					ret += ">";
				}
				if (i < top) ret += ", ";
			}
			return ret;
		}

		struct create_class
		{
			static int stage2(lua_State* L)
			{

				class_rep* crep = static_cast<class_rep*>(lua_touserdata(L, lua_upvalueindex(1)));
				assert((crep != 0) && "internal error, please report");
				assert((is_class_rep(L, lua_upvalueindex(1))) && "internal error, please report");

#ifndef LUABIND_NO_ERROR_CHECKING

				if (!is_class_rep(L, 1))
				{
					lua_pushstring(L, "expected class to derive from or a newline");
					lua_error(L);
				}

#endif

				class_rep* base = static_cast<class_rep*>(lua_touserdata(L, 1));

				class_rep::base_info binfo;

				binfo.pointer_offset = 0;
				binfo.base = base;
				crep->add_base(binfo);

				if (base->get_class_type() == class_rep::lua_class)
				{
					// copy base class members
					
					detail::getref(L, crep->table_ref());
					detail::getref(L, base->table_ref());
					lua_pushnil(L);

					while (lua_next(L, -2))
					{	
						lua_pushvalue(L, -2); // copy key
						lua_insert(L, -2);
						lua_settable(L, -5);
					}
				}

				crep->set_type(base->type());
				
				return 0;
			}

			static int stage1(lua_State* L)
			{

#ifndef LUABIND_NO_ERROR_CHECKING

				if (lua_gettop(L) != 1 || lua_type(L, 1) != LUA_TSTRING || lua_isnumber(L, 1))
				{
					lua_pushstring(L, "invalid construct, expected class name");
					lua_error(L);
				}

#endif

				const char* name = lua_tostring(L, 1);

				void* c = lua_newuserdata(L, sizeof(class_rep));

				lua_pushvalue(L, -1);
				// the global name of the class is registered inside this constructor
				new(c) class_rep(L, name);
				lua_pushcclosure(L, &stage2, 1);

				return 1;
			}
		};

		template<class Type>
		struct register_wrapped_type
		{
			template<class Signature, class Policies>
			static void apply(detail::construct_rep::overload_t& o, const Signature*, const Policies*)
			{
				o.set_wrapped_constructor(
					&detail::construct_wrapped_class<Type, Policies, Signature>::apply
				); 
			}
		};

		template<>
		struct register_wrapped_type<detail::null_type>
		{
			template<class Signature, class Policies>
			static void apply(detail::construct_rep::overload_t&, const Signature*, const Policies*) {}
		};


		// if the class is held by a smart pointer, we need to be able to
		// implicitly dereference the pointer when needed.

		template<class UnderlyingT, class HeldT>
		struct extract_underlying_type
		{
			static void* extract(void* ptr)
			{
				HeldT& held_obj = *reinterpret_cast<HeldT*>(ptr);
				UnderlyingT* underlying_ptr = get_pointer(held_obj);
				return underlying_ptr;
			}
		};


		template<class HeldType>
		struct internal_held_type_extractor
		{
			template<class T>
			static void*(*)(void*) apply(detail::type<T>)
			{
				return detail::extract_underlying_type<T, HeldType>::extract;
			}
		};

		template<>
		struct internal_held_type_extractor<detail::null_type>
		{
			template<class T>
			static void*(*)(void*) apply(detail::type<T>)
			{
				return 0;
			}
		};

		template<class HeldType>
		struct internal_held_type
		{
			static LUABIND_TYPE_INFO apply()
			{
				return LUABIND_TYPEID(HeldType);
											detail::extract_underlying_type<
											T, HeldType>::extract);

				if (r->classes.find(LUABIND_TYPEID(HeldType)) == r->classes.end())
				{
					r->classes[LUABIND_TYPEID(HeldType)] = crep;
				}
			}
		};

		template<>
		struct internal_held_type<detail::null_type>
		{
			static LUABIND_TYPE_INFO apply()
			{
				return LUABIND_INVALID_TYPE_INFO;
			}
		};

	} // detail














	struct class_base: detail::scoped_object
	{
	private:

		struct base_desc
		{
			LUABIND_TYPE_INFO type;
			int ptr_offset;
		};

#ifndef NDEBUG
		bool m_cloned;
#endif

#ifdef LUABIND_DONT_COPY_STRINGS
		const char* m_name;
#else
		std::string m_name;
#endif

		std::map<const char*, detail::method_rep, ltstr> m_methods;

		// datamembers, some members may be readonly, and
		// only have a getter function
		std::map<const char*, detail::class_rep::callback, ltstr> m_getters;
		std::map<const char*, detail::class_rep::callback, ltstr> m_setters;

		// the operators in lua
		std::vector<detail::class_rep::operator_callback> m_operators[number_of_operators]; 
		std::map<const char*, int, detail::ltstr> m_static_constants;

		std::vector<base_desc> m_bases;
		detail::construct_rep m_constructor;
		detail::construct_rep m_wrapped_constructor;

		void(*m_destructor)(void*);
		void*(*m_extractor)(void*);

		LUABIND_TYPE_INFO m_type;
		LUABIND_TYPE_INFO m_held_type;

#ifndef LUABIND_DONT_COPY_STRINGS
		// the maps that contains char pointers points into
		// this vector of strings. 
		std::vector<char*> m_strings;
#endif

	protected:

		void add_method(const char* name, const detail::overload_rep& o)
		{
#ifdef LUABIND_DONT_COPY_STRINGS
			detail::method_rep& method = m_methods[name];
			method.name = name;
#else
			m_strings.push_back(detail::dup_string(name));
			detail::method_rep& method = m_methods[m_strings.back()];
			method.name = m_strings.back();
#endif
			method.add_overload(o);
			method.crep = 0;
		}

		void add_constructor(const detail::construct_rep::overload_t& o)
		{
			m_constructor.overloads.push_back(o);
		}

		void add_wrapped_constructor(const detail::construct_rep::overload_t& o)
		{
			m_wrapped_constructor.overloads.push_back(o);
		}

		inline void add_getter(const char* name, const boost::function2<int, lua_State*, int>& g)
		{
			callback c;
			c.func = g;
			c.pointer_offset = 0;
#ifndef LUABIND_DONT_COPY_STRINGS
			m_strings.push_back(dup_string(name));
			m_getters[m_strings.back()] = c;
#else
			m_getters[name] = c;
#endif
		}

		inline void add_setter(const char* name, const boost::function2<int, lua_State*, int>& s)
		{
			callback c;
			c.func = s;
			c.pointer_offset = 0;
#ifndef LUABIND_DONT_COPY_STRINGS
			m_strings.push_back(dup_string(name));
			m_setters[m_strings.back()] = c;
#else
			m_setters[name] = c;
#endif
		}



	public:

		class_base()
		{
#ifndef NDEBUG
			m_cloned = false;
#endif
		}

		virtual ~class_base()
		{
// if we are copying strings, we have to destroy them too
#ifndef LUABIND_DONT_COPY_STRINGS
			for (std::vector<char*>::iterator i = m_strings.begin(); i != m_strings.end(); ++i)
				delete[] *i;
#endif
		}

		virtual void commit(lua_State* L)
		{
			detail::class_rep* crep;

			detail::class_registry* r = detail::class_registry::get_registry(L);
			// create a class_rep structure for this class.
			// allocate it within lua to let lua collect it on
			// lua_close(). This is better than allocating it
			// as a static, since it will then be destructed
			// when the program exits instead.
			// warning: we assume that lua will not
			// move the userdata memory.
			lua_newuserdata(L, sizeof(detail::class_rep));
			crep = reinterpret_cast<detail::class_rep*>(lua_touserdata(L, -1));

			new(crep) detail::class_rep(m_type, m_name, L, m_destructor, m_held_type, m_extractor);

			// register this new type in the class registry
			r->add_class(LUABIND_TYPEID(T), crep);


			// add bases
			for_each(m_bases.begin(), m_bases.end(), boost::bind(&crep::add_base, crep, _1));

			// add methods
			for (std::map<const char*, detail::method_rep, ltstr>::iterator i = m_methods.begin();
										i != m_methods.end(); 
										++i)
			{
				i->crep = crep;
			}
			swap(crep->m_methods, m_methods);

			// constructors
			m_constructor.swap(crep->m_constructor);
			m_wrapped_constructor.swap(crep->m_wrapped_constructor);

			
			/* ... */
		}

		// destructive copy
		virtual scoped_object* clone() const
		{
			assert(m_cloned == false);

#ifndef NDEBUG
			m_cloned = true;
#endif

			class_base* ret = new class_base();
#ifdef LUABIND_DONT_COPY_STRING
			ret->m_name = m_name;
#else
			std::swap(ret->m_name, m_name);
#endif

			std::swap(ret->m_getters, m_getters);
			std::swap(ret->m_setters, m_setters);
			std::swap(ret->m_operators, m_operators);
			std::swap(ret->m_static_constants, m_static_constants);
			ret->m_destructor = m_destructor;

			std::swap(ret->m_bases, m_bases);
			std::swap(ret->m_methods, m_methods);
			m_constructor.swap(ret->m_constructor);
			m_wrapped_constructor.swap(ret->m_wrapped_constructor);

#ifndef LUABIND_DONT_COPY_STRINGS
			std::swap(ret->m_strings, m_strings);
#endif

		}

	};











	// registers a class in the lua environment
	template<class T, class X1, class X2, class X3>
	struct class_: class_base 
	{
		typedef class_<T, X1, X2, X3> self_t;

	private:

		lua_State* m_L;

		template<class A, class B, class C, class D>
		class_(const class_<A,B,C,D>&);

	public:

		// WrappedType MUST inherit from T
		typedef typename detail::extract_parameter<
				boost::mpl::vector3<X1,X2,X3>
			,  boost::is_base_and_derived<T, boost::mpl::_>
	/*		,	boost::mpl::not_<
						boost::mpl::or_<
								detail::is_bases<boost::mpl::_>
							,	boost::is_base_and_derived<boost::mpl::_, T>
						>
				>*/
				,	detail::null_type
		>::type WrappedType;

		typedef typename detail::extract_parameter<
			  boost::mpl::vector3<X1,X2,X3>
			, boost::mpl::not_<
				boost::mpl::or_<
					boost::mpl::or_<
						  detail::is_bases<boost::mpl::_>
						, boost::is_base_and_derived<boost::mpl::_, T>
						>
					, boost::is_base_and_derived<T, boost::mpl::_>
				>
			>
		  , detail::null_type
		>::type HeldType;

		// this function generates conversion information
		// in the given class_rep structure. It will be able
		// to implicitly cast to the given template type
		template<class To>
		void gen_base_info(detail::type<To>)
		{
			// fist, make sure the given base class is registered.
			// if it's not registered we can't push it's lua table onto
			// the stack because it doesn't have a table

			// try to cast this type to the base type and remember
			// the pointer offset. For multiple inheritance the pointer
			// may change when casting. Since we need to be able to
			// cast we need this pointer offset.
			// store the information in this class' base class-vector
			base_desc base;
			base.type = LUABIND_TYPEID(To);
			T* null_pointer = reinterpret_cast<T*>(1); // this wont work if the pointer is 0!
			base.ptr_offset = static_cast<int>((reinterpret_cast<char*>(static_cast<To*>(null_pointer)) - reinterpret_cast<char*>(null_pointer)));
			add_base(base);
		}

		void gen_base_info(detail::type<detail::null_type>)
		{}

#define LUABIND_GEN_BASE_INFO(z, n, text) gen_base_info(crep, reg, detail::type<B##n>());

		template<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_BASES, class B)>
		void generate_baseclass_list(detail::class_rep* crep, detail::class_registry* reg, detail::type<bases<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_BASES, B)> >)
		{
			BOOST_PP_REPEAT(LUABIND_MAX_BASES, LUABIND_GEN_BASE_INFO, _)
		}

#undef LUABIND_GEN_BASE_INFO
	
		// this is the internal version of def() it is run from both overloads
		// of def. It has two versions, one where a contstructor is registered
		// and one where a function is registered
		template<class Policies>
		struct internal_def_s
		{
			template<class F>
			static void apply(const char* name, F f, detail::class_rep* crep)
			{
//				std::cout << "HeldType2: " << typeid(HeldType).name() << "\n";

				detail::overload_rep o(f, static_cast<Policies*>(0));

				typedef LUABIND_MSVC_TYPENAME detail::function_callback_s<HeldType,T,F,Policies>::type call_t;

				o.set_match_fun(&detail::match_function_callback_s<T,F,Policies>::apply);
				o.call_fun = boost::bind<int>(call_t(f), _1, _2);

#ifndef LUABIND_NO_ERROR_CHECKING

				o.set_sig_fun(&detail::get_member_signature<F>::apply);

#endif

				crep->add_function(name, o);
			}

			template<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, class A)>
			static void apply(constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)>, detail::class_rep* crep)
			{
//				std::cout << "HeldType2: " << typeid(HeldType).name() << "\n";

				detail::construct_rep::overload_t o;

				o.set_constructor(
					&detail::construct_class<
						 T
						,Policies
						,constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)>
					>::apply
				);

				// if we have a WrappedType, we have to register it's constructor
				// but if it's null_type (no WrappedType) we should not register it
				detail::register_wrapped_type<WrappedType>::apply(o,
						static_cast<const constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)>*>(0),
						static_cast<const Policies*>(0));


				o.set_match_fun(
					&detail::constructor_match<
						 constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)>
						,2
						,Policies
					>::apply);

#ifndef LUABIND_NO_ERROR_CHECKING

				o.set_sig_fun(&detail::get_signature<constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)> >::apply);

#endif

				typedef constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)> con_t;

				o.set_arity(detail::calc_arity<con_t::arity>::apply(con_t(), static_cast<Policies*>(0)));

				crep->add_constructor(o);
			}
		};

		class_(lua_State* L_, const char* name_): L(L_), name(name_)
		{
/*
					std::cout << "X1: " << typeid(X1).name() << "\n";
					std::cout << "X2: " << typeid(X2).name() << "\n";
					std::cout << "X3: " << typeid(X3).name() << "\n";
					std::cout << "HeldType1: " << typeid(HeldType).name() << "\n";
*/

			typedef typename detail::extract_parameter<
					boost::mpl::vector3<X1,X2,X3>
				,	boost::mpl::or_<
							detail::is_bases<boost::mpl::_>
						,	boost::is_base_and_derived<boost::mpl::_, T>
					>
				,	no_bases
			>::type bases_t;

			typedef typename 
				boost::mpl::if_<detail::is_bases<bases_t>
					,	bases_t
					,	bases<bases_t>
				>::type Base;
	
			m_type = LUABIND_TYPEID(T);
			m_held_type = detail::internal_held_type<HeldType>::apply();
			m_extractor = detail::internal_held_type_extractor<HeldType>::apply(detail::type<T>());
			generate_baseclass_list(r, detail::type<Base>());

//			std::cout << "held type: " << typeid(HeldType).name() << '\n';
		}

		template<class F>
		class_& def(const char* name, F f)
		{
			internal_def_s<detail::null_type>::apply(name, f);
			return *this;
		}

		template<class F, class Policies>
		class_& def(const char* name, F f, const Policies&)
		{
			internal_def_s<Policies>::apply(name, f);
			return *this;
		}

		template<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, class A)>
		class_& def(constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)> sig)
		{
			internal_def_s<detail::null_type>::apply(sig);
			return *this;
		}

		template<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, class A), class Policies>
		class_& def(constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)> sig, const Policies& policies)
		{
			internal_def_s<Policies>::apply(sig);
			return *this;
		}

		template<class Getter>
		class_& property(const char* name, Getter g)
		{
			m_crep->add_getter(name, boost::bind<int>(detail::get_caller<T, Getter, detail::null_type>(), _1, _2, g));
			return *this;
		}

		template<class Getter, class MaybeSetter>
		class_& property(const char* name, Getter g, MaybeSetter s)
		{
			return property_impl(name, g, s, boost::mpl::bool_<detail::is_policy_cons<MaybeSetter>::value>());
		}

		template<class Getter, class Setter, class GetPolicies>
		class_& property(const char* name, Getter g, Setter s, const GetPolicies& get_policies)
		{
			add_getter(name, boost::bind<int>(detail::get_caller<T, Getter, GetPolicies>(get_policies), _1, _2, g));
			add_setter(name, boost::bind<int>(detail::set_caller<T, Setter, detail::null_type>(), _1, _2, s));
			return *this;
		}

		template<class Getter, class Setter, class GetPolicies, class SetPolicies>
		class_& property(const char* name
									, Getter g, Setter s
									, const GetPolicies& get_policies
									, const SetPolicies& set_policies)
		{
			add_getter(name, boost::bind<int>(detail::get_caller<T, Getter, GetPolicies>(get_policies), _1, _2, g));
			add_setter(name, boost::bind<int>(detail::set_caller<T, Setter, GetPolicies>(set_policies), _1, _2, s));
			return *this;
		}

		template<class D>
		class_& def_readonly(const char* name, D T::*member_ptr)
		{
			add_getter(name, boost::bind<int>(detail::auto_get<T,D,detail::null_type>(), _1, _2, member_ptr));
			return *this;
		}

		template<class D, class Policies>
		class_& def_readonly(const char* name, D T::*member_ptr, const Policies& policies)
		{
			add_getter(name, boost::bind<int>(detail::auto_get<T,D,Policies>(policies), _1, _2, member_ptr));
			return *this;
		}

		template<class D>
		class_& def_readwrite(const char* name, D T::*member_ptr)
		{
			add_getter(name, boost::bind<int>(detail::auto_get<T,D,detail::null_type>(), _1, _2, member_ptr));
			add_setter(name, boost::bind<int>(detail::auto_set<T,D,detail::null_type>(), _1, _2, member_ptr));
			return *this;
		}

		template<class D, class GetPolicies>
		class_& def_readwrite(const char* name, D T::*member_ptr, const GetPolicies& get_policies)
		{
			add_getter(name, boost::bind<int>(detail::auto_get<T,D,GetPolicies>(get_policies), _1, _2, member_ptr));
			add_setter(name, boost::bind<int>(detail::auto_set<T,D,detail::null_type>(), _1, _2, member_ptr));
			return *this;
		}

		template<class D, class GetPolicies, class SetPolicies>
		class_& def_readwrite(const char* name, D T::*member_ptr, const GetPolicies& get_policies, const SetPolicies& set_policies)
		{
			add_getter(name, boost::bind<int>(detail::auto_get<T,D,GetPolicies>(get_policies), _1, _2, member_ptr));
			add_setter(name, boost::bind<int>(detail::auto_set<T,D,SetPolicies>(set_policies), _1, _2, member_ptr));
			return *this;
		}

		template<class op_id, class Left, class Right, class Policies>
		class_& def(detail::operator_<op_id, Left, Right>, const Policies& policies)
		{
			typedef typename detail::operator_unwrapper<Policies, op_id, T, Left, Right> op_type;
#ifndef LUABIND_NO_ERROR_CHECKING
			add_operator(L
													, op_type::get_id()
													, &op_type::execute
													, &op_type::match
													, &detail::get_signature<constructor<typename op_type::left_t, typename op_type::right_t> >::apply
													, detail::is_unary(op_type::get_id()) ? 1 : 2);
#else
			add_operator(L
													, op_type::get_id()
													, &op_type::execute
													, &op_type::match
													, detail::is_unary(op_type::get_id()) ? 1 : 2);
#endif
			return *this;
		}

		template<class op_id, class Left, class Right>
		class_& def(detail::operator_<op_id, Left, Right>)
		{
			typedef typename detail::operator_unwrapper<detail::null_type, op_id, T, Left, Right> op_type;

#ifndef LUABIND_NO_ERROR_CHECKING
			add_operator(L
													, op_type::get_id()
													, &op_type::execute
													, &op_type::match
													, &detail::get_signature<constructor<LUABIND_MSVC_TYPENAME op_type::left_t, LUABIND_MSVC_TYPENAME op_type::right_t> >::apply
													, detail::is_unary(op_type::get_id()) ? 1 : 2);
#else
			add_operator(L
													, op_type::get_id()
													, &op_type::execute
													, &op_type::match
													, detail::is_unary(op_type::get_id()) ? 1 : 2);
#endif
			return *this;
		}

		template<class Signature, bool Constant>
		class_& def(detail::application_operator<Signature, Constant>*)
		{
			typedef detail::application_operator<Signature, Constant, detail::null_type> op_t;

			int arity = detail::calc_arity<Signature::arity>::apply(Signature(), static_cast<detail::null_type*>(0));

#ifndef LUABIND_NO_ERROR_CHECKING
			add_operator(L, detail::op_call, &op_t::template apply<T>::execute, &op_t::match, &detail::get_signature<Signature>::apply, arity + 1);
#else
			add_operator(L, detail::op_call, &op_t::template apply<T>::execute, &op_t::match, arity + 1);
#endif

			return *this;
		}

		template<class Signature, bool Constant, class Policies>
		class_& def(detail::application_operator<Signature, Constant>*, const Policies& policies)
		{
			typedef detail::application_operator<Signature, Constant, Policies> op_t;

			int arity = detail::calc_arity<Signature::arity>::apply(Signature(), static_cast<Policies*>(0));

#ifndef LUABIND_NO_ERROR_CHECKING

			add_operator(L, detail::op_call, &op_t::template apply<T>::execute, &op_t::match, &detail::get_signature<Signature>::apply, arity + 1);
#else
			add_operator(L, detail::op_call, &op_t::template apply<T>::execute, &op_t::match, arity + 1);
#endif

			return *this;
		}

		detail::enum_maker<self_t> enum_(const char*)
		{
			return detail::enum_maker<self_t>(*this);
		}

	private:

		template<class Getter, class GetPolicies>
		class_& property_impl(const char* name,
									 Getter g,
									 GetPolicies policies,
									 boost::mpl::bool_<true>)
		{
			add_getter(name, boost::bind<int>(detail::get_caller<T,Getter,GetPolicies>(policies), _1, _2, g));
			return *this;
		}

		template<class Getter, class Setter>
		class_& property_impl(const char* name,
									 Getter g,
									 Setter s,
									 boost::mpl::bool_<false>)
		{
			add_getter(name, boost::bind<int>(detail::get_caller<T,Getter,detail::null_type>(), _1, _2, g));
			add_setter(name, boost::bind<int>(detail::set_caller<T,Setter,detail::null_type>(), _1, _2, s));
			return *this;
		}

	};
}

#endif // LUABIND_CLASS_HPP_INCLUDED
