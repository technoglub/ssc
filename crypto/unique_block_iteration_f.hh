/* Copyright (c) 2019-2020 Stuart Steven Calder
 * All rights reserved.
 * See accompanying LICENSE file for licensing information.
 */
#pragma once

#include <cstdio>
#include <cstdlib>
#include <ssc/crypto/constants.hh>
#include <ssc/crypto/operations.hh>
#include <ssc/crypto/threefish_f.hh>
#include <ssc/general/integers.hh>
#include <ssc/general/macros.hh>

#if    defined (DEFAULT_ARGS)     || defined (TEMPLATE_ARGS)       || defined (CLASS)                 || \
       defined (REKEY_CIPHER_XOR) || defined (MODIFY_TWEAK_FLAGS)  || defined (MODIFY_TWEAK_POSITION) || \
       defined (INIT_TWEAK)
#	error 'Some MACRO we need was already defined'
#endif

#define DEFAULT_ARGS	template <int Bits, Key_Schedule_E Key_Sch = Key_Schedule_E::Runtime_Compute>

#define TEMPLATE_ARGS	template <int Bits, Key_Schedule_E Key_Sch>

#define CLASS		Unique_Block_Iteration_F<Bits,Key_Sch>

#define REKEY_CIPHER_XOR(dat_ptr) \
	Threefish_f::rekey( &(dat_ptr->threefish_data), dat_ptr->key_state, dat_ptr->tweak_state ); \
	Threefish_f::cipher( &(dat_ptr->threefish_data), reinterpret_cast<u8_t*>(dat_ptr->key_state), dat_ptr->msg_state ); \
	xor_block<State_Bits>( dat_ptr->key_state, dat_ptr->msg_state )

#define MODIFY_TWEAK_FLAGS(dat_ptr,operation,value) \
	reinterpret_cast<u8_t*>(dat_ptr->tweak_state)[ Tweak_Bytes - 1 ] operation value

#define MODIFY_TWEAK_POSITION(dat_ptr,operation,value) \
	dat_ptr->tweak_state[ 0 ] operation static_cast<u64_t>(value)

#define INIT_TWEAK(dat_ptr,init_bitwise_or) \
	std::memset( dat_ptr->tweak_state, 0, Tweak_Bytes ); \
	MODIFY_TWEAK_FLAGS (dat_ptr,|=,(Tweak_First_Bit|init_bitwise_or))

namespace ssc
{
	DEFAULT_ARGS
	class Unique_Block_Iteration_F
	{
	public:
	/* Compile-Time checks, Constatns, and Aliases */
		static_assert (CHAR_BIT == 8);
		static_assert (Bits % 8 == 0);
		using Threefish_f = Threefish_F<Bits,Key_Sch>;

		_CTIME_CONST (int) State_Bits = Bits;
		_CTIME_CONST (int) State_Bytes = State_Bits / CHAR_BIT;
		_CTIME_CONST (int) Tweak_Bits = Threefish_f::Tweak_Bits;
		_CTIME_CONST (int) Tweak_Bytes = Tweak_Bits / CHAR_BIT;
		_CTIME_CONST (Key_Schedule_E) Threefish_KS = Key_Sch;

		Unique_Block_Iteration_F (void) = delete;

		enum class Type_Mask_E : u8_t {
			Key =  0,
			Cfg =  4,
			Prs =  8,
			Pk  = 12,
			Kdf = 16,
			Non = 20,
			Msg = 48,
			Out = 63
		};
		_CTIME_CONST (u8_t) Tweak_First_Bit  = 0b0100'0000;
		_CTIME_CONST (u8_t) Tweak_First_Mask = ~(Tweak_First_Bit);
		_CTIME_CONST (u8_t) Tweak_Last_Bit   = 0b1000'0000;

		static_assert (State_Bytes == Threefish_f::Block_Bytes);
		struct Data {
			typename Threefish_f::Data_t threefish_data;
			u64_t                        key_state   [Threefish_f::External_Key_Words];
			alignas(u64_t) u8_t          msg_state   [State_Bytes];
			u64_t                        tweak_state [Threefish_f::External_Tweak_Words];
		};

		static void chain_config        (Data *data, u64_t const num_out_bits);

		static void chain_native_output (_RESTRICT (Data *) data,
				                 _RESTRICT (u8_t *) output);

		static void chain_message       (_RESTRICT (Data *)       data,
				                 _RESTRICT (u8_t const *) input,
						 u64_t                    num_in_bytes);

		static void chain_output        (_RESTRICT (Data *) data,
				                 _RESTRICT (u8_t *) output,
						 u64_t              num_out_bytes);

		template <Type_Mask_E Type,int Input_Bytes>
		static void chain_type (_RESTRICT (Data *)       data,
				        _RESTRICT (u8_t const *) input);
	/* Constructors / Destructors */
	};/* ~ class Unique_Block_Iteration_F */

	TEMPLATE_ARGS
	void CLASS::chain_config (Data *data, u64_t const num_out_bits)
	{
		INIT_TWEAK            (data,(Tweak_Last_Bit | static_cast<u8_t>(Type_Mask_E::Cfg)));
		MODIFY_TWEAK_POSITION (data,=,32);

		/*
		 * Layout of the configuration string
		 *
		   _CTIME_CONST (u8_t) config [32] = {
			// First 4 bytes
			0x53, 0x48, 0x41, 0x33, // Schema identifier "SHA3"
			// Next 2 bytes
			0x01, 0x00, // Version number (1)
			// Next 2 bytes
			0x00, 0x00, // Reserved (0)
			// Next 8 bytes
			0x00, 0x00, 0x00, 0x00, // Output length
			0x00, 0x00, 0x00, 0x00,
			// Remaining 16 bytes
			0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00
		   };
		*/
		_CTIME_CONST (u8_t) first_5_config_bytes [5] = {
			0x53, 0x48, 0x41, 0x33, // Schema identifier "SHA3"
			0x01 // Version number (1)
		};
		// Only copy in the first 5 bytes of the config string since the rest are zeroes.
		std::memcpy( data->msg_state, first_5_config_bytes, sizeof(first_5_config_bytes) );
		// Manually zero over the rest of the config string.
		std::memset( (data->msg_state + 5), 0, (sizeof(data->msg_state) - 5) );
		// Set the "output length" portion of the config string.
#if 0
		/* Remove undefined-behavior type-punning */
		*(reinterpret_cast<u64_t*>(data->msg_state + 8)) = num_out_bits;
#else
		std::memcpy( data->msg_state + 8, &num_out_bits, sizeof(num_out_bits) );
#endif
		REKEY_CIPHER_XOR (data);
	}

	TEMPLATE_ARGS
	void CLASS::chain_native_output (_RESTRICT (Data *) data,
			                 _RESTRICT (u8_t *) output)
	{
		// Set the tweak first bit, last bit, and the type to Out.
		INIT_TWEAK            (data,(Tweak_Last_Bit | static_cast<u8_t>(Type_Mask_E::Out)));
		// Set the tweak position to State_Bytes.
		MODIFY_TWEAK_POSITION (data,=,sizeof(u64_t));
		// Zero over the message state.
		std::memset( data->msg_state, 0, sizeof(data->msg_state) );
		REKEY_CIPHER_XOR (data);
		// Copy the key state into the output buffer.
		std::memcpy( output, data->key_state, State_Bytes );
	}

	TEMPLATE_ARGS
	void CLASS::chain_message (_RESTRICT (Data *)       data,
			           _RESTRICT (u8_t const *) input,
				   u64_t                    num_in_bytes)
	{
		// Set the tweak first bit and the type to Msg.
		INIT_TWEAK (data,static_cast<u8_t>(Type_Mask_E::Msg));
		if( num_in_bytes <= State_Bytes ) { // If there is one or less blocks worth of input...
			// Set the tweak last bit.
			MODIFY_TWEAK_FLAGS    (data,|=,Tweak_Last_Bit);
			// Set the tweak position to the number of input bytes.
			MODIFY_TWEAK_POSITION (data,=,num_in_bytes);
			// Copy the whole input into the message state.
			std::memcpy( data->msg_state, input, num_in_bytes );
			// If the input was not an entire block, zero over the rest of the message state.
			std::memset( (data->msg_state + num_in_bytes), 0, (sizeof(data->msg_state) - num_in_bytes) );
			REKEY_CIPHER_XOR (data);
			return;
		} else { // If there is more than one block worth of input to begin with...
			// Set the tweak position to the number of state bytes.
			MODIFY_TWEAK_POSITION (data,=,State_Bytes);
			// Copy a block worth of input into the message state.
			std::memcpy( data->msg_state, input, State_Bytes );
			REKEY_CIPHER_XOR   (data);
			// Clear the tweak first bit.
			MODIFY_TWEAK_FLAGS (data,&=,Tweak_First_Mask);
			// Decrement bytes left, increment input pointer.
			num_in_bytes -= State_Bytes;
			input        += State_Bytes;
		}
		while( num_in_bytes > State_Bytes ) { // While there is more than one block of input left to process...
			// Increment the tweak position by State_Bytes.
			MODIFY_TWEAK_POSITION (data,+=,State_Bytes);
			// Copy a block worth of input into the message state.
			std::memcpy( data->msg_state, input, State_Bytes );
			REKEY_CIPHER_XOR (data);
			// Decrement bytes left, increment input pointer.
			num_in_bytes -= State_Bytes;
			input        += State_Bytes;
		}
		// Set the tweak last bit.
		MODIFY_TWEAK_FLAGS    (data,|=,Tweak_Last_Bit);
		// Increment the tweak position by the number of bytes left, for the last block.
		MODIFY_TWEAK_POSITION (data,+=,num_in_bytes);
		// Copy the remaining input bytes into the message state.
		std::memcpy( data->msg_state, input, num_in_bytes );
		// If less than 1 block of input was left, zero over the remaining bytes of the message state.
		std::memset( (data->msg_state + num_in_bytes), 0, (sizeof(data->msg_state) - num_in_bytes) );
		REKEY_CIPHER_XOR (data);
	}
	TEMPLATE_ARGS
	void CLASS::chain_output (_RESTRICT (Data *) data,
			          _RESTRICT (u8_t *) output,
				  u64_t              num_out_bytes)
	{
		INIT_TWEAK (data,static_cast<u8_t>(Type_Mask_E::Out));
		std::memset( data->msg_state, 0, sizeof(data->msg_state) );
		MODIFY_TWEAK_POSITION (data,=,sizeof(u64_t));
		if( num_out_bytes <= State_Bytes ) {
			MODIFY_TWEAK_FLAGS    (data,|=,Tweak_Last_Bit);
			REKEY_CIPHER_XOR      (data);
			std::memcpy( output, data->key_state, num_out_bytes );
			return;
		} else {
			REKEY_CIPHER_XOR      (data);
			MODIFY_TWEAK_FLAGS    (data,&=,Tweak_First_Mask);
			std::memcpy( output, data->key_state, State_Bytes );
#if 0
			/* Remove undefined-behavior type-punning */
			*(reinterpret_cast<u64_t*>(data->msg_state)) += 1;
#else
			{
				u64_t temp;
				std::memcpy( &temp, data->msg_state, sizeof(temp) );
				++temp;
				std::memcpy( data->msg_state, &temp, sizeof(temp) );
			}
#endif
			num_out_bytes -= State_Bytes;
			output        += State_Bytes;

			while( num_out_bytes > State_Bytes ) {
				MODIFY_TWEAK_POSITION (data,+=,sizeof(u64_t));
				REKEY_CIPHER_XOR (data);
				std::memcpy( output, data->key_state, State_Bytes );
#if 0
				/* Remove undefined-behavior type-punning */
				*(reinterpret_cast<u64_t*>(data->msg_state)) += 1;
#else
				{
					u64_t temp;
					std::memcpy( &temp, data->msg_state, sizeof(temp) );
					++temp;
					std::memcpy( data->msg_state, &temp, sizeof(temp) );
				}
#endif
				num_out_bytes -= State_Bytes;
				output        += State_Bytes;
			}
			MODIFY_TWEAK_FLAGS (data,|=,Tweak_Last_Bit);
			MODIFY_TWEAK_POSITION (data,+=,sizeof(u64_t));
			REKEY_CIPHER_XOR (data);
			std::memcpy( output, data->key_state, num_out_bytes );
		}
	}
	TEMPLATE_ARGS template <typename CLASS::Type_Mask_E Type,int Input_Bytes>
	void CLASS::chain_type (_RESTRICT (Data *)       data,
			        _RESTRICT (u8_t const *) input)
	{
		static_assert (Type == Type_Mask_E::Key ||
			       Type == Type_Mask_E::Prs ||
			       Type == Type_Mask_E::Pk  ||
			       Type == Type_Mask_E::Kdf ||
			       Type == Type_Mask_E::Non,
			       "Do not use 'chain_type' for types described elsewhere.");
		static_assert (Input_Bytes >= 1);

		std::memset( data->tweak_state, 0, Tweak_Bytes );
		INIT_TWEAK (data,static_cast<u8_t>(Type));
		if constexpr (Input_Bytes <= State_Bytes) {
			MODIFY_TWEAK_FLAGS    (data,|=,Tweak_Last_Bit);
			MODIFY_TWEAK_POSITION (data,=,Input_Bytes);
			std::memcpy( data->msg_state, input, Input_Bytes );
			if constexpr (Input_Bytes != State_Bytes)
				std::memset( (data->msg_state + Input_Bytes), 0, (State_Bytes - Input_Bytes) );
			REKEY_CIPHER_XOR (data);
			return;
		} else {
			MODIFY_TWEAK_POSITION (data,=,State_Bytes);
			std::memcpy( data->msg_state, input, State_Bytes );
			input += State_Bytes;
			REKEY_CIPHER_XOR   (data);
			MODIFY_TWEAK_FLAGS (data,&=,Tweak_First_Mask);
			int bytes_left = (Input_Bytes - State_Bytes);
			while( bytes_left > State_Bytes ) {
				MODIFY_TWEAK_POSITION (data,+=,State_Bytes);
				std::memcpy( data->msg_state, input, State_Bytes );
				input      += State_Bytes;
				bytes_left -= State_Bytes;
				REKEY_CIPHER_XOR (data);
			}
			MODIFY_TWEAK_FLAGS    (data,|=,Tweak_Last_Bit);
			MODIFY_TWEAK_POSITION (data,+=,bytes_left);
			std::memcpy( data->msg_state, input, bytes_left );
			if constexpr (Input_Bytes % State_Bytes != 0)
				std::memset( (data->msg_state + bytes_left), 0, (State_Bytes - bytes_left) );
			REKEY_CIPHER_XOR (data);
		}
	}
}/* ~ namespace ssc */
#undef INIT_TWEAK
#undef MODIFY_TWEAK_POSITION
#undef MODIFY_TWEAK_FLAGS
#undef REKEY_CIPHER_XOR
#undef CLASS
#undef TEMPLATE_ARGS
#undef DEFAULT_ARGS
