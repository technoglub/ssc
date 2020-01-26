/*
Copyright (c) 2019 Stuart Steven Calder
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once

#include <climits>
#include <cstdlib>
#include <cstring>
#include <ssc/crypto/operations.hh>
#include <ssc/general/integers.hh>
#include <ssc/general/symbols.hh>
#if 0 // Seems to be unneeded now.
#	include <ssc/memory/os_memory_locking.hh>
#endif

#ifndef CTIME_CONST
#	define CTIME_CONST(type) static constexpr const type
#else
#	error 'Already defined'
#endif

namespace ssc {
	template <int Key_Bits>
	class Threefish {
	public:
		/* Static Checks */
		static_assert ((Key_Bits == 256 || Key_Bits == 512 || Key_Bits == 1024), "Invalid keysize");
		static_assert ((CHAR_BIT == 8), "This implementation needs 8-bit chars");
		/* Public Constants */
		CTIME_CONST(u64_t)	Constant_240  = 0x1b'd1'1b'da'a9'fc'1a'22;
		CTIME_CONST(int)	Block_Bits    = Key_Bits;
		CTIME_CONST(int)	Block_Bytes   = Block_Bits / CHAR_BIT;
		CTIME_CONST(int)	Number_Words  = Key_Bits / 64;
		CTIME_CONST(int)	Tweak_Words   = 2;
		CTIME_CONST(int)	Tweak_Bytes   = Tweak_Words * sizeof(u64_t);
		CTIME_CONST(int)	Number_Rounds = [](int bits) {
								if (bits == 1024)
									return 80;
								return 72;
							}( Key_Bits );
		CTIME_CONST(int)	Number_Subkeys = (Number_Rounds / 4) + 1;

		CTIME_CONST(int)	State_Words        = Number_Words;
		CTIME_CONST(int)	State_Bytes	   = State_Words * sizeof(u64_t);

		CTIME_CONST(int)	Buffer_Key_Schedule_Words = Number_Subkeys * Number_Words;
		CTIME_CONST(int)	Buffer_Key_Schedule_Bytes = Buffer_Key_Schedule_Words * sizeof(u64_t);

		CTIME_CONST(int)	Buffer_Tweak_Words      = Tweak_Words + 1;
		CTIME_CONST(int)	Buffer_Tweak_Bytes	= Buffer_Tweak_Words * sizeof(u64_t);

		CTIME_CONST(int)	Buffer_Key_Words	= Number_Words + 1;
		CTIME_CONST(int)	Buffer_Key_Bytes	= Buffer_Key_Words * sizeof(u64_t);

		CTIME_CONST(int)	Buffer_Words	   = (State_Words + Buffer_Key_Schedule_Words + Buffer_Tweak_Words + Buffer_Key_Words);
		CTIME_CONST(int)	Buffer_Bytes       = (State_Bytes + Buffer_Key_Schedule_Bytes + Buffer_Tweak_Bytes + Buffer_Key_Bytes);
		static_assert		(Buffer_Bytes == (Buffer_Words * sizeof(u64_t)));
		/* Constructors / Destructors */
		Threefish (void) = delete;

		Threefish (u64_t *buffer)
			: state{ buffer },
			  key_schedule{ buffer + State_Words },
			  key{ buffer + (State_Words + Buffer_Key_Schedule_Words) },
			  tweak{ buffer + (State_Words + Buffer_Key_Schedule_Words + Buffer_Key_Words) }
		{
		}

		Threefish (u64_t *__restrict buffer, u8_t const *__restrict k, u8_t const *__restrict tw = nullptr)
			: state{ buffer },
			  key_schedule{ buffer + State_Words },
			  key{ buffer + (State_Words + Buffer_Key_Schedule_Words) },
			  tweak{ buffer + (State_Words + Buffer_Key_Schedule_Words + Buffer_Key_Words) }
		{
			expand_key_( k, tw );
		}
		/* Public Functions */
		void
		cipher (u8_t *out, u8_t const *in);

		void
		inverse_cipher (u8_t *out, u8_t const *in);

		void
		rekey (u8_t const *__restrict new_key, u8_t const *__restrict new_tweak = nullptr);
	private:
		u64_t	* const state;
		u64_t	* const key_schedule;
		u64_t	* const key;
		u64_t	* const tweak;
		/* Private Functions */
		static void
		mix_		(u64_t *__restrict x0, u64_t *__restrict x1, int const round, int const index);

		static void
		inverse_mix_	(u64_t *__restrict x0, u64_t *__restrict x1, int const round, int const index);

		void
		expand_key_	(u8_t const *__restrict key, u8_t const *__restrict tweak);

		void
		add_subkey_	(int const round);

		void
		subtract_subkey_	(int const round);

		static unsigned int
		get_rotate_constant_	(int const round, int const index);

		void
		permute_state_	(void);

		void
		inverse_permute_state_	(void);
	}; /* class Threefish */

	template <int Key_Bits>
	void
	Threefish<Key_Bits>::rekey (u8_t const *__restrict new_key,
				    u8_t const *__restrict new_tweak)
	{
		// Expand the key and tweak arguments into the keyschedule.
		expand_key_( new_key, new_tweak );
	}

	template <int Key_Bits>
	void
	Threefish<Key_Bits>::mix_ (u64_t *__restrict x0, u64_t *__restrict x1, int const round, int const index) {
		// x0 equal the sum of x0 and x1.
		(*x0) = ((*x0) + (*x1));
		// x1 equals x1 rotated right by a rotation constant defined by the round number and index XOR'd with x0.
		(*x1) = ( rotate_left<u64_t>( (*x1), get_rotate_constant_( round, index ) ) ^ (*x0) );
	}

	template <int Key_Bits>
	void
	Threefish<Key_Bits>::inverse_mix_	(u64_t *__restrict x0, u64_t *__restrict x1, int const round, int const index) {
		// x1 equls x0 XOR'd with x1.
		(*x1) = ((*x0) ^ (*x1));
		// x1 equals x1 rotated right by a rotation constant defined by the round number and index.
		(*x1) = rotate_right<u64_t>( (*x1), get_rotate_constant_( round, index ) ) ;
		// x0 equals x0 minus x1.
		(*x0) = (*x0) - (*x1);
	}

	template <int Key_Bits>
	unsigned int
	Threefish<Key_Bits>::get_rotate_constant_ (int const round, int const index) {
		static_assert (Key_Bits == 256 || Key_Bits == 512 || Key_Bits == 1024);
		// All rotation constant look-up tables.
		if constexpr(Key_Bits == 256) {
			static constexpr unsigned int const rc [8][2] = {
				{ 14, 16 }, //d = 0
				{ 52, 57 }, //d = 1
				{ 23, 40 }, //d = 2
				{  5, 37 }, //d = 3
				{ 25, 33 }, //d = 4
				{ 46, 12 }, //d = 5
				{ 58, 22 }, //d = 6
				{ 32, 32 }  //d = 7
			};
			return rc[ (round % 8) ][ index ] ;
		} else if constexpr(Key_Bits == 512) {
			static constexpr unsigned int const rc [8][4] = {
				{ 46, 36, 19, 37 },
				{ 33, 27, 14, 42 },
				{ 17, 49, 36, 39 },
				{ 44,  9, 54, 56 },
				{ 39, 30, 34, 24 },
				{ 13, 50, 10, 17 },
				{ 25, 29, 39, 43 },
				{  8, 35, 56, 22 }
			};
			return rc[ (round % 8) ][ index ];
		} else if constexpr(Key_Bits == 1024) {
			static constexpr unsigned int const rc [8][8] = {
				{ 24, 13,  8, 47,  8, 17, 22, 37 },
				{ 38, 19, 10, 55, 49, 18, 23, 52 },
				{ 33,  4, 51, 13, 34, 41, 59, 17 },
				{  5, 20, 48, 41, 47, 28, 16, 25 },
				{ 41,  9, 37, 31, 12, 47, 44, 30 },
				{ 16, 34, 56, 51,  4, 53, 42, 41 },
				{ 31, 44, 47, 46, 19, 42, 44, 25 },
				{  9, 48, 35, 52, 23, 31, 37, 20 }
			};
			return rc[ (round % 8) ][ index ];
		}
	}/* ! get_rotate_constant_ */

	template <int Key_Bits>
	void
	Threefish<Key_Bits>::expand_key_ (u8_t const *__restrict k, u8_t const *__restrict tw) {
		using std::memcpy, std::memset;
		memcpy( key, k, State_Bytes );
		key[ Number_Words ] = Constant_240;
		for (int i = 0; i < Number_Words; ++i)
			key[ Number_Words ] ^= key[ i ];
		if (tw != nullptr) {
			memcpy( tweak, tw, Tweak_Bytes );
			tweak[ 2 ] = tweak[ 0 ] ^ tweak[ 1 ];
		} else {
			memset( tweak, 0, Buffer_Tweak_Bytes );
		}

		for (int subkey = 0; subkey < Number_Subkeys; ++subkey) {// for each subkey
			int const subkey_index = subkey * Number_Words;
			for (int i = 0; i <= Number_Words - 4; ++i)// for each word of the subkey
				key_schedule[ subkey_index + i ] = key[ (subkey + i) % (Number_Words + 1) ];
			key_schedule[ subkey_index + (Number_Words - 3) ] =  key[ (subkey + (Number_Words - 3)) % (Number_Words + 1) ] + tweak[ subkey % 3 ];
			key_schedule[ subkey_index + (Number_Words - 2) ] =  key[ (subkey + (Number_Words - 2)) % (Number_Words + 1) ] + tweak[ (subkey + 1) % 3 ];
			key_schedule[ subkey_index + (Number_Words - 1) ] =  key[ (subkey + (Number_Words - 1)) % (Number_Words + 1) ] + static_cast<u64_t>(subkey);
		}
	} /* expand_key_ */

	template <int Key_Bits>
	void
	Threefish<Key_Bits>::add_subkey_	(int const round) {
		int const subkey = round / 4;
		int const offset = subkey * Number_Words;
		for (int i = 0; i < Number_Words; ++i)
			state[ i ] += key_schedule[ offset + i ];
	}

	template <int Key_Bits>
	void
	Threefish<Key_Bits>::subtract_subkey_	(int const round) {
		int const subkey = round / 4;
		int const offset = subkey * Number_Words;
		for (int i = 0; i < Number_Words; ++i)
			state[ i ] -= key_schedule[ offset + i ];
	}

	template <int Key_Bits>
	void
	Threefish<Key_Bits>::cipher (u8_t *out, u8_t const *in) {
		using std::memcpy;

		memcpy( state, in, State_Bytes );
		for (int round = 0; round < Number_Rounds; ++round) {
			if (round % 4 == 0)
				add_subkey_( round );
			for (int j = 0; j <= ((Number_Words / 2) - 1); ++j)
				mix_( (state + (j*2)), (state + (j*2) + 1), round, j );
			permute_state_();
		}
		add_subkey_( Number_Rounds );
		memcpy( out, state, State_Bytes );
	} /* cipher */

	template <int Key_Bits>
	void
	Threefish<Key_Bits>::inverse_cipher (u8_t *out, u8_t const *in) {
		using std::memcpy;
		
		memcpy( state, in, State_Bytes );
		subtract_subkey_( Number_Rounds );
		for (int round = Number_Rounds - 1; round >= 0; --round) {
			inverse_permute_state_();
			for (int j = 0; j <= ((Number_Words / 2) - 1); ++j)
				inverse_mix_( (state + (j*2)), (state + (j*2) + 1), round, j );
			if (round % 4 == 0)
				subtract_subkey_( round );
		}
		memcpy( out, state, State_Bytes );
	} /* inverse_cipher */

	template <int Key_Bits>
	void
	Threefish<Key_Bits>::permute_state_	(void) {
		if constexpr(Key_Bits == 256) {
			u64_t w = state[ 1 ];
			state[ 1 ] = state[ 3 ];
			state[ 3 ] = w;
		} else if constexpr(Key_Bits == 512) {
			u64_t w0, w1;
			/* Start from the left. Shift words in and out as necessary
			Starting with index 0 ...*/
			// index 0 overwrites index 6
			w0 = state[ 6 ];
			state[ 6 ] = state[ 0 ];
			// original index 6 (currently w0)
			// overwrites index 4 (saved into w1)
			w1 = state[ 4 ];
			state[ 4 ] = w0;
			// original index 4 (currently w1)
			// overwrites index 2 (saved into w0)
			w0 = state[ 2 ];
			state[ 2 ] = w1;
			// original index 2 (currently w0)
			// overwrites index 0 (doesn't need to be saved, as it was already written into state[6]
			state[ 0 ] = w0;

			/* Index 1 and 5 don't move. All that's left is to swap index 3 and index 7 */
			w0 = state[ 3 ];
			state[ 3 ] = state[ 7 ];
			state[ 7 ] = w0;
		} else if constexpr(Key_Bits == 1024) {
			u64_t w0, w1;
			// 1 overwrites 15 (stored in w0)
			w0 = state[ 15 ];
			state[ 15 ] = state[ 1 ];
			// 15 (in w0) overwrites 7 (stored in w1)
			w1 = state[ 7 ];
			state[ 7 ] = w0;
			// 7 (in w1) overwrites 9 (stored in w0)
			w0 = state[ 9 ];
			state[ 9 ] = w1;
			// 9 (in w0) overwrites 1
			state[ 1 ] = w0;

			// 3 overwrites 11 (stored in w0)
			w0 = state[ 11 ];
			state[ 11 ] = state[ 3 ];
			// 11 (in w0) overwrites 5 (stored in w1)
			w1 = state[ 5 ];
			state[ 5 ] = w0;
			// 5 (in w1) overwrites 13 (stored in w0)
			w0 = state[ 13 ];
			state[ 13 ] = w1;
			// 13 (in w0) overwrites 3
			state[ 3 ] = w0;

			// 4 and 6 are swapped
			w0 = state[ 4 ];
			state[ 4 ] = state[ 6 ];
			state[ 6 ] = w0;

			// 8 overwrites 14 (stored in w0)
			w0 = state[ 14 ];
			state[ 14 ] = state[ 8 ];
			// 14 (in w0) overwrites 12 (stored in w1)
			w1 = state[ 12 ];
			state[ 12 ] = w0;
			// 12 (in w1) overwrites 10 (stored in w0)
			w0 = state[ 10 ];
			state[ 10 ] = w1;
			// 10 (in w0) overwrites 8
			state[ 8 ] = w0;
		}
	} /* permute_state_ */

	template <int Key_Bits>
	void
	Threefish<Key_Bits>::inverse_permute_state_	(void) {
		static_assert (Key_Bits == 256 || Key_Bits == 512 || Key_Bits == 1024);
		if constexpr(Key_Bits == 256) {
			permute_state_();  // here, permute_state() and inverse_permute_state() are the same operation
		} else if constexpr(Key_Bits == 512) {
			u64_t w0, w1;
			/* Starting from the left with index 0 */
			// original index 0
			// overwrites original index 2 (saved into w0)
			w0 = state[ 2 ];
			state[ 2 ] = state[ 0 ];
			// original index 2 (currently in w0)
			// overwrites original index 4 (saved into w1)
			w1 = state[ 4 ];
			state[ 4 ] = w0;
			// original index 4 (currently in w1)
			// overwrites original index 6 (saved into w0)
			w0 = state[ 6 ];
			state[ 6 ] = w1;
			// original index 6 (currently in w0)
			// overwrites original index 0 (doesnt need to be saved)
			state[ 0 ] = w0;
			/* Index 1 and 5 don't move. All that's left is to swap index 3 and index 7 */
			w0 = state[ 3 ];
			state[ 3 ] = state[ 7 ];
			state[ 7 ] = w0;
		} else if constexpr(Key_Bits == 1024) {
			u64_t w0, w1;
			// 1 overwrites 9 (stored in w0)
			w0 = state[ 9 ];
			state[ 9 ] = state[ 1 ];
			// 9 (in w0) overwrites 7 (stored in w1)
			w1 = state[ 7 ];
			state[ 7 ] = w0;
			// 7 (in w1) overwrites 15 (stored in w0)
			w0 = state[ 15 ];
			state[ 15 ] = w1;
			// 15 (in w0) overwrites 1
			state[ 1 ] = w0;

			// 3 overwrites 13 (stored in w0)
			w0 = state[ 13 ];
			state[ 13 ] = state[ 3 ];
			// 13 (in w0) overwrites 5 (stored in w1)
			w1 = state[ 5 ];
			state[ 5 ] = w0;
			// 5 (in w1) overwrites 11 (stored in w0)
			w0 = state[ 11 ];
			state[ 11 ] = w1;
			// 11 (in w0) overwrites 3
			state[ 3 ] = w0;

			// 4 and 6 are swapped
			w0 = state[ 4 ];
			state[ 4 ] = state[ 6 ];
			state[ 6 ] = w0;

			// 8 overwrites 10 (stored in w0)
			w0 = state[ 10 ];
			state[ 10 ] = state[ 8 ];
			// 10 (in w0) overwrites 12 (stored in w1)
			w1 = state[ 12 ];
			state[ 12 ] = w0;
			// 12 (in w1) overwrites 14 (stored in w0)
			w0 = state[ 14 ];
			state[ 14 ] = w1;
			// 14 (in w0) overwrites 8
			state[ 8 ] = w0;
		}
	}/* inverse_permute_state */
} /* ! namespace ssc */
#undef CTIME_CONST
