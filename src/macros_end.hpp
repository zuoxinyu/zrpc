#undef FIRST_
#undef SECOND_

#undef FIRST
#undef SECOND

#undef EMPTY

#undef EVAL
#undef EVAL1024   //(...) EVAL512(EVAL512(__VA_ARGS__))
#undef EVAL512    //(...) EVAL256(EVAL256(__VA_ARGS__))
#undef EVAL256    //(...) EVAL128(EVAL128(__VA_ARGS__))
#undef EVAL128    //(...) EVAL64(EVAL64(__VA_ARGS__))
#undef EVAL64     //(...) EVAL32(EVAL32(__VA_ARGS__))
#undef EVAL32     //(...) EVAL16(EVAL16(__VA_ARGS__))
#undef EVAL16     //(...) EVAL8(EVAL8(__VA_ARGS__))
#undef EVAL8      //(...) EVAL4(EVAL4(__VA_ARGS__))
#undef EVAL4      //(...) EVAL2(EVAL2(__VA_ARGS__))
#undef EVAL2      //(...) EVAL1(EVAL1(__VA_ARGS__))
#undef EVAL1      //(...) __VA_ARGS__

#undef DEFER1   //(m) m EMPTY()
#undef DEFER2   //(m) m EMPTY EMPTY()()

#undef IS_PROBE   //(...) SECOND(__VA_ARGS__, 0)
#undef PROBE      //() ~, 1

#undef CAT   ////(a,b) a ## b

#undef NOT      ////(x) IS_PROBE(CAT(_NOT_, x))
#undef _NOT_0   // PROBE//()

#undef BOOL   //(x) NOT(NOT(x))

#undef IF_ELSE    ////(condition) _IF_ELSE(BOOL(condition))
#undef _IF_ELSE   ////(condition) CAT(_IF_, condition)

#undef _IF_1   //(...) __VA_ARGS__ _IF_1_ELSE
#undef _IF_0   //(...)             _IF_0_ELSE

#undef _IF_1_ELSE   //(...)
#undef _IF_0_ELSE   //(...) __VA_ARGS__

#undef HAS_ARGS             //(...) BOOL(FIRST(_END_OF_ARGUMENTS_ __VA_ARGS__)())
#undef _END_OF_ARGUMENTS_   //() 0

#undef MAP
#undef _MAP

#undef DERIVE_PACKABLE
#undef DERIVE_FORMMATABLE_STRUCT
#undef DERIVE_FORMMATABLE_ENUM
