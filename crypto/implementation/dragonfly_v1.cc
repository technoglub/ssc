/* Copyright (c) 2019-2020 Stuart Steven Calder
 * All rights reserved.
 * See accompanying LICENSE file for licensing information.
 */
#include "dragonfly_v1.hh"
using namespace std;

// Just in case we need to do this.
#define __STDC_FORMAT_MACROS
#include <cinttypes>

#if    defined (LOCK_MEMORY_) || defined (UNLOCK_MEMORY_)
#       error 'Some MACRO we need was already defined'
#endif

#ifdef SHIM_FEATURE_MEMORYLOCKING
#	define LOCK_MEMORY_(address, size)	shim_lock_memory( address, size )
#       define UNLOCK_MEMORY_(address, size)	shim_unlock_memory( address, size )
#else
#       define LOCK_MEMORY_(none_0,none_1)
#       define UNLOCK_MEMORY_(non_0,none_1)
#endif

#define CLEANUP_MAP_(map) \
	shim_unmap_memory( &map ); \
	shim_close_file( map.shim_file )

#define CLEANUP_ERROR_(secret_object) \
	shim_secure_zero( &secret_object, sizeof(secret_object) ); \
	UNLOCK_MEMORY_ (&secret_object, sizeof(secret_object)); \
	CLEANUP_MAP_ (output_map); \
	CLEANUP_MAP_ (input_map); \
	remove( output_filename )

#define CLEANUP_SUCCESS_(secret_object) \
	shim_secure_zero( &secret_object, sizeof(secret_object) ); \
	UNLOCK_MEMORY_ (&secret_object, sizeof(secret_object)); \
	shim_sync_map( &output_map ); \
	CLEANUP_MAP_ (output_map); \
	CLEANUP_MAP_ (input_map)

namespace ssc::crypto_impl::dragonfly_v1 {
	void
	encrypt (Catena_Input const & SHIM_RESTRICT catena_input,
		 Shim_Map &           SHIM_RESTRICT input_map,
		 Shim_Map &           SHIM_RESTRICT output_map,
		 char const *         SHIM_RESTRICT output_filename)
	{
		{// Setup the output map.
			// Assume the output file's size to be the plaintext size with a visible header.
			output_map.size = input_map.size + Visible_Metadata_Bytes + catena_input.padding_bytes;
			shim_set_file_size( output_map.shim_file, output_map.size ); // Set the output file's size.
			shim_map_memory( &output_map, false );
		}

		static_assert (Block_Bytes == Threefish_f::Block_Bytes);
		struct { // Secret data, that will be memory-locked and destroyed after use.
			CTR_Data_t              ctr_data;
			union { // Only one variant of CATENA will be used (with Phi or without Phi).
				typename Catena_Strong_f::Data strong;
				typename Catena_Safe_f::Data   safe;
			} catena;
			// One key for encryption, large enough to hold the parity word computed during the Threefish keyschedule generation.
			uint64_t                   enc_key         [Threefish_f::External_Key_Words];
			// One key for authenticaiton, just large enough for the authentication code in Skein_f::mac().
			alignas(uint64_t) uint8_t  auth_key        [Threefish_f::Block_Bytes];
			typename UBI_f::Data       ubi_data;
			// Two password buffers. One for the initial input, the second to compare and ensure the intended password is used without error.
			uint8_t                    first_password  [Password_Buffer_Bytes];
			uint8_t                    second_password [Password_Buffer_Bytes];
			typename CSPRNG_f::Data    csprng_data;
			// A buffer used to temporarily hold random input from the keyboard, that will be hashed into the CSPRNG's seed buffer to strengthen it.
			uint8_t                    entropy_data    [Supplement_Entropy_Buffer_Bytes];
			// A buffer to hold the output of CATENA in the first `Block_Bytes` of memory, that will then be hashed into `Block_Bytes*2` bytes of memory,
			// where the first `Block_Bytes` of memory will be used as the encryption key, and the second `Block_Bytes` of memory will be used as the
			// authentication key.
			alignas(uint64_t) uint8_t  hash_output     [Block_Bytes * 2];

		} secret;
		struct { // Public data, that need not be destroyed after use.
			// Threefish tweak buffer; large enough to store the parity word during generation of the Threefish keyschedule.
			uint64_t                  tf_tweak    [Threefish_f::External_Tweak_Words];
			// Enough bytes to copy into the nonce portion of the counter-mode's keystream buffer.
			alignas(uint64_t) uint8_t ctr_nonce   [CTR_f::IV_Bytes];
			// Enough salt bytes to harden CATENA against adversaries with dedicated hardware.
			alignas(uint64_t) uint8_t catena_salt [Salt_Bytes];
		} pub;

		LOCK_MEMORY_ (&secret,sizeof(secret)); // Lock the secret object into memory; do not swap it to disk.

		int password_size; // Store the number of bytes in the password we're about to get.

		{// Obtain the password.
			Terminal_UI_f::init();
			password_size = Terminal_UI_f::obtain_password( secret.first_password,
					                                secret.second_password,
									Password_Prompt,
									Password_Reentry_Prompt );
			shim_secure_zero( secret.second_password, Password_Buffer_Bytes ); // Destroy.
		}
		CSPRNG_f::initialize_seed( &secret.csprng_data ); // Initialize the random number generator.
		if( catena_input.supplement_os_entropy ) { // Supplement the RNG's entropy if specified to do so.
			supplement_entropy( &secret.csprng_data, secret.entropy_data, secret.entropy_data + Block_Bytes );
			shim_secure_zero( secret.entropy_data, sizeof(secret.entropy_data) );
		}
		Terminal_UI_f::end();
		{// 3 calls to the RNG to generate the tweak, nonce, and salt.
			CSPRNG_f::get( &secret.csprng_data,
				       reinterpret_cast<uint8_t*>(pub.tf_tweak),
				       Tweak_Bytes ); // Generate 16 bytes of tweak material.
			CSPRNG_f::get( &secret.csprng_data,
				       pub.ctr_nonce,
				       sizeof(pub.ctr_nonce) ); // Generate 32 bytes of nonce material for counter-mode.
			CSPRNG_f::get( &secret.csprng_data,
				       pub.catena_salt,
				       sizeof(pub.catena_salt) ); // Generate 32 bytes of salt material for CATENA.
			shim_secure_zero( &secret.csprng_data, sizeof(secret.csprng_data) );
		}
		{
			// Generate the catena output, that we will then hash into encryption and authentication keys.
			if( !catena_input.use_phi ) { // Resistance to cache-timing adversaries branch.
				memcpy( secret.catena.safe.salt,
					pub.catena_salt,
					sizeof(pub.catena_salt) ); // Copy the salt into CATENA's salt buffer.
				auto r = Catena_Safe_f::call( &secret.catena.safe, // Use the safe member of the CATENA union.
						              secret.hash_output,    // Output into the first `Block_Bytes` of secret.hash_output.
						              secret.first_password, // Process the password buffer.
						              password_size,         // Process `password_size` bytes of the password buffer.
						              catena_input.g_low,    // Pass in the lower memory-bound, g_low.
						              catena_input.g_high,   // Pass in the upper memory-bound, g_high.
						              catena_input.lambda ); // Pass in the time-cost parameter, lambda.
				if( r != Catena_Safe_f::Return_E::Success ) { // CATENA failed to allocate memory branch.
					CLEANUP_ERROR_ (secret);
					SHIM_ERRX ("Error: Catena_Safe_f failed with error code %d...\n"
						   "Allocating too much memory?\n", static_cast<int>(r));
				}
				shim_secure_zero( &secret.catena.safe, sizeof(secret.catena.safe) );
			} else { // Resistance to massively-parallel adversaries branch.
				// Copy the salt in.
				memcpy( secret.catena.strong.salt,
				        pub.catena_salt,
					sizeof(pub.catena_salt) ); // Copy the salt into CATENA's salt buffer.
				auto r = Catena_Strong_f::call( &secret.catena.strong, // Use the strong member of the CATENA union.
					  	                secret.hash_output,      // Output into the first `Block_Bytes` of secret.hash_output.
						                secret.first_password,   // Process the password buffer.
						                password_size,           // Proces `password_size` bytes of the password buffer.
						                catena_input.g_low,      // Pass in the lower memory-bound, g_low.
						                catena_input.g_high,     // Pass in the upper memory-bound, g_high.
						                catena_input.lambda );   // Pass in the time-cost parameter, lambda.
				if( r != Catena_Strong_f::Return_E::Success ) { // CATENA failed to allocate memory branch.
					CLEANUP_ERROR_ (secret);
					SHIM_ERRX ("Error: Catena_Strong_f failed with error code %d...\n"
						   "Allocating too much memory?\n", static_cast<int>(r));
				}
				shim_secure_zero( &secret.catena.strong, sizeof(secret.catena.strong) );
			}
			shim_secure_zero( &secret.first_password, sizeof(secret.first_password) );
			static_assert (sizeof(secret.hash_output) == (Block_Bytes * 2)); // Hash the output into encryption and authentication keys.
			Skein_f::hash( &secret.ubi_data,
				       secret.hash_output, // Output into itself.
				       secret.hash_output, // Use the output of CATENA as input to Skein.
				       Block_Bytes,        // Process the first `Block_Bytes` of data, as output by CATENA.
				       (Block_Bytes * 2) );// Output `Block_Bytes * 2` bytes of pseudorandom data into the buffer.
			memcpy( secret.enc_key,
				secret.hash_output,
				Block_Bytes ); // Copy the first Block into the encryption key buffer.
			memcpy( secret.auth_key,
				secret.hash_output + Block_Bytes,
				Block_Bytes ); // Copy the second Block into the authentication key buffer.
			shim_secure_zero( secret.hash_output, sizeof(secret.hash_output) );
			Threefish_f::rekey( &secret.ctr_data.threefish_data,
					    secret.enc_key,
					    pub.tf_tweak ); // Compute the Threefish_f keyschedule with the encryption key and tweak.
		}
		// Setup the public portion of the header
		uint8_t *out = output_map.ptr; // Get a pointer to the beginning of the memory-mapped output file.

		memcpy( out, Dragonfly_V1_ID, sizeof(Dragonfly_V1_ID) ); // Copy the Dragonfly_V1 identifier in.
		out += sizeof(Dragonfly_V1_ID);
		std::memcpy( out, &output_map.size, sizeof(output_map.size) );
		out += sizeof(uint64_t);
		(*out++) = catena_input.g_low;                           // Copy the lower memory-bound in.
		(*out++) = catena_input.g_high;                          // Copy the upper memory-bound in.
		(*out++) = catena_input.lambda;                          // Copy the time-cost parameter in.
		(*out++) = catena_input.use_phi;                         // Copy the phi-usage parameter in.
		memcpy( out, pub.tf_tweak, Tweak_Bytes );                // Copy the Threefish tweak in.
		out += Tweak_Bytes;
		memcpy( out, pub.catena_salt, Salt_Bytes );              // Copy the CATENA salt in.
		out += Salt_Bytes;
		memcpy( out, pub.ctr_nonce, CTR_f::IV_Bytes);        // Copy the CTR mode nonce in.
		out += CTR_f::IV_Bytes;
		{
			uint64_t crypt_header [2] = { 0 };
			crypt_header[ 0 ] = catena_input.padding_bytes;
			CTR_f::set_iv( &secret.ctr_data,
				       pub.ctr_nonce );
			CTR_f::xorcrypt( &secret.ctr_data,
					 out,
					 reinterpret_cast<uint8_t*>(crypt_header),
					 sizeof(crypt_header) );
			out += sizeof(crypt_header);
			if( catena_input.padding_bytes != 0 ) {
				CTR_f::xorcrypt( &secret.ctr_data,
						 out,
						 out,
						 catena_input.padding_bytes,
						 sizeof(crypt_header) );
				out += catena_input.padding_bytes;
			}
			CTR_f::xorcrypt( &secret.ctr_data,
					 out,
					 input_map.ptr,
					 input_map.size,
					 sizeof(crypt_header) + catena_input.padding_bytes );
			out += input_map.size;

		}
		{
			Skein_f::mac( &secret.ubi_data,
				      out,
				      output_map.ptr,
				      secret.auth_key,
				      MAC_Bytes,
				      output_map.size - MAC_Bytes ); // Authenticate the ciphertext with the auth key we generated earlier.
		}
		CLEANUP_SUCCESS_ (secret);
	}/* ~ void encrypt (...) */
	void
	decrypt (Shim_Map &   SHIM_RESTRICT input_map,
		 Shim_Map &   SHIM_RESTRICT output_map,
		 char const * SHIM_RESTRICT output_filename)
	{
		output_map.size = input_map.size - Visible_Metadata_Bytes;

		static constexpr int Minimum_Possible_File_Size = Visible_Metadata_Bytes + 1;
		if( input_map.size < Minimum_Possible_File_Size ) {
			shim_close_file( output_map.shim_file );
			remove( output_filename );
			CLEANUP_MAP_ (input_map);
			SHIM_ERRX ("Error: Input file doesn't appear to be large enough to be a SSC_DRAGONFLY_V1 encrypted file\n");
		}
		uint8_t const *in = input_map.ptr;
		struct {
			uint64_t                  tweak       [Threefish_f::External_Key_Words];
			alignas(uint64_t) uint8_t catena_salt [Salt_Bytes];
			alignas(uint64_t) uint8_t ctr_nonce   [CTR_f::IV_Bytes];
			uint64_t                  header_size;
			uint8_t                   header_id   [sizeof(Dragonfly_V1_ID)];
			uint8_t                   g_low;
			uint8_t                   g_high;
			uint8_t                   lambda;
			uint8_t                   use_phi;
		} pub;
		// Copy all the fields of the Dragonfly_V1 header from the memory-mapped input file into the pub struct.
		{
			memcpy( pub.header_id, in, sizeof(pub.header_id) );
			in += sizeof(pub.header_id);
			std::memcpy( &pub.header_size, in, sizeof(pub.header_size) );
			in += sizeof(uint64_t);
			pub.g_low   = (*in++);
			pub.g_high  = (*in++);
			pub.lambda  = (*in++);
			pub.use_phi = (*in++);
			memcpy( pub.tweak, in, Tweak_Bytes );
			in += Tweak_Bytes;
			memcpy( pub.catena_salt, in, Salt_Bytes );
			in += Salt_Bytes;
			memcpy( pub.ctr_nonce, in, CTR_f::IV_Bytes);
			in += CTR_f::IV_Bytes;
		}
		if( memcmp( pub.header_id, Dragonfly_V1_ID, sizeof(Dragonfly_V1_ID) ) != 0 ) {
			shim_unmap_memory( &input_map );
			shim_close_file( input_map.shim_file );
			shim_close_file( output_map.shim_file );
			remove( output_filename );
			SHIM_ERRX ("Error: Not a Dragonfly_V1 encrypted file.\n");
		}
		struct {
			CTR_Data_t           ctr_data;
			typename UBI_f::Data ubi_data;
			union {
				typename Catena_Strong_f::Data strong;
				typename Catena_Safe_f::Data   safe;
			} catena;
			uint64_t                   enc_key  [Threefish_f::External_Key_Words];
			alignas(uint64_t) uint8_t  auth_key [Block_Bytes];
			alignas(uint64_t) uint8_t  hash_buf [Block_Bytes * 2];
			uint8_t                    password [Password_Buffer_Bytes];
			alignas(uint64_t) uint8_t  gen_mac  [MAC_Bytes];
		} secret;

		LOCK_MEMORY_ (&secret, sizeof(secret));

		Terminal_UI_f::init();
		int password_size = Terminal_UI_f::obtain_password( secret.password, Password_Prompt );
		Terminal_UI_f::end();
		if( !pub.use_phi ) {
			memcpy( secret.catena.safe.salt,
				pub.catena_salt,
				sizeof(pub.catena_salt) );
			auto r = Catena_Safe_f::call( &secret.catena.safe,
					              secret.hash_buf,
						      secret.password,
						      password_size,
						      pub.g_low,
						      pub.g_high,
						      pub.lambda );
			if( r != Catena_Safe_f::Return_E::Success ) {
				shim_secure_zero( &secret, sizeof(secret) );
				UNLOCK_MEMORY_ (&secret, sizeof(secret));
				CLEANUP_MAP_ (input_map);
				shim_close_file( output_map.shim_file );
				remove( output_filename );
				SHIM_ERRX ("Error: Catena_Safe_f failed with error code %d...\n"
					   "Do you have enough memory to decrypt this file?\n", static_cast<int>(r));
			}
			shim_secure_zero( &secret.catena.safe, sizeof(secret.catena.safe) );
		} else {
			memcpy( secret.catena.strong.salt,
				pub.catena_salt,
				sizeof(pub.catena_salt) );
			auto r = Catena_Strong_f::call( &secret.catena.strong,
					                secret.hash_buf,
							secret.password,
							password_size,
							pub.g_low,
							pub.g_high,
							pub.lambda );
			if( r != Catena_Strong_f::Return_E::Success ) {
				shim_secure_zero( &secret, sizeof(secret) );
				UNLOCK_MEMORY_ (&secret, sizeof(secret));
				CLEANUP_MAP_ (input_map);
				shim_close_file( output_map.shim_file );
				remove( output_filename );
				SHIM_ERRX ("Error: Catena_Strong_f failed with error code %d...\n"
					   "Do you have enough memory to decrypt this file?\n", static_cast<int>(r));
			}
			shim_secure_zero( &secret.catena.strong, sizeof(secret.catena.strong) );
		}
		{// Generate the keys.
			Skein_f::hash( &secret.ubi_data,
				       secret.hash_buf,
				       secret.hash_buf,
				       Block_Bytes,
				       (Block_Bytes * 2) );
			memcpy( secret.enc_key,
				secret.hash_buf,
				Block_Bytes );
			memcpy( secret.auth_key,
				secret.hash_buf + Block_Bytes,
				Block_Bytes );
			shim_secure_zero( secret.hash_buf, sizeof(secret.hash_buf) );
			{
				Skein_f::mac( &secret.ubi_data,
					      secret.gen_mac,
					      input_map.ptr,
					      secret.auth_key,
					      sizeof(secret.gen_mac),
					      input_map.size - MAC_Bytes );
				if( shim_ctime_memcmp( secret.gen_mac, (input_map.ptr + input_map.size - MAC_Bytes), MAC_Bytes ) != 0 ) {
					shim_secure_zero( &secret, sizeof(secret) );
					UNLOCK_MEMORY_ (&secret, sizeof(secret));
					CLEANUP_MAP_ (input_map);
					shim_close_file( output_map.shim_file );
					remove( output_filename );
					SHIM_ERRX ("Error: Authentication failed.\n"
						   "Possibilities: Wrong password, the file is corrupted, or it has been tampered with.\n");
				}
			}
			Threefish_f::rekey( &secret.ctr_data.threefish_data,
					    secret.enc_key,
					    pub.tweak );
		}
		{
			// Set the nonce.
			CTR_f::set_iv( &secret.ctr_data,
				       pub.ctr_nonce );
			uint64_t padding_bytes;
			// Decrypt the padding bytes.
			CTR_f::xorcrypt( &secret.ctr_data,
					 reinterpret_cast<uint8_t*>(&padding_bytes),
					 in,
					 sizeof(padding_bytes) );
			output_map.size -= padding_bytes;
			shim_set_file_size( output_map.shim_file, output_map.size );
			shim_map_memory( &output_map, false );
			in += (padding_bytes + (sizeof(uint64_t) * 2)); // Skip the second word. It is reserved.
			CTR_f::xorcrypt( &secret.ctr_data,
					 output_map.ptr,
					 in,
					 output_map.size,
					 (sizeof(uint64_t) * 2) + padding_bytes );
		}
		shim_secure_zero( &secret, sizeof(secret) );
		UNLOCK_MEMORY_ (&secret, sizeof(secret));
		shim_sync_map( &output_map );
		CLEANUP_MAP_ (output_map);
		CLEANUP_MAP_ (input_map);
	}/* ~ void decrypt (...) */
	void
	dump_header (Shim_Map &                 input_map,
		     char const * SHIM_RESTRICT input_filename)
	{
		static constexpr int Minimum_Size = Visible_Metadata_Bytes + 1;
		if( input_map.size < Minimum_Size ) {
			CLEANUP_MAP_ (input_map);
			SHIM_ERRX ("File %s looks too small to be SSC_Dragonfly_V1 encrypted\n", input_filename);
		}
		struct {
			uint8_t  id    [sizeof(Dragonfly_V1_ID)];
			uint64_t total_size;
			uint8_t  g_low;
			uint8_t  g_high;
			uint8_t  lambda;
			uint8_t  use_phi;
			uint8_t  tweak [Tweak_Bytes];
			uint8_t  salt  [Salt_Bytes];
			uint8_t  nonce [CTR_f::IV_Bytes];
		} header;
		uint8_t mac [MAC_Bytes];
		{
			uint8_t const *p = input_map.ptr;
			memcpy( header.id, p, sizeof(header.id) );
			p += sizeof(header.id);
			memcpy( &header.total_size, p, sizeof(header.total_size) );
			p += sizeof(header.total_size);
			header.g_low   = (*p++);
			header.g_high  = (*p++);
			header.lambda  = (*p++);
			header.use_phi = (*p++);
			memcpy( header.tweak, p, sizeof(header.tweak) );
			p += sizeof(header.tweak);
			memcpy( header.salt, p, sizeof(header.salt) );
			p += sizeof(header.salt);
			memcpy( header.nonce, p, sizeof(header.nonce) );
			p += sizeof(header.nonce);
			p = input_map.ptr + input_map.size - MAC_Bytes;
			memcpy( mac, p, sizeof(mac) );
		}
		CLEANUP_MAP_ (input_map);

		header.id[ sizeof(header.id) - 1 ] = '\0';
		fprintf( stdout, "File Header ID : %s\n", reinterpret_cast<char*>(header.id) );
		fprintf( stdout, "File Size      : %" PRIu64 "\n", header.total_size );
		fprintf( stdout, "Garlic Low     : %d\n", static_cast<int>(header.g_low) );
		fprintf( stdout, "Garlic High    : %d\n", static_cast<int>(header.g_high) );
		fprintf( stdout, "Lambda         : %d\n", static_cast<int>(header.lambda) );
		if( !header.use_phi )
			fprintf( stdout, "The Phi function is not used.\n" );
		else
			fprintf( stdout, "The Phi function is used!\n" );
		fputs(           "Threefish Tweak :\n", stdout );
		shim_print_byte_buffer( header.tweak, sizeof(header.tweak) );
		fputs(         "\nCatena Salt :\n", stdout );
		shim_print_byte_buffer( header.salt, sizeof(header.salt) );
		fputs(         "\nCTR Initialization Vector:\n", stdout );
		shim_print_byte_buffer( header.nonce, sizeof(header.nonce) );
		fputs(         "\nSkein-MAC:\n", stdout );
		shim_print_byte_buffer( mac, sizeof(mac) );
		fputs( "\n", stdout );
	}
}/* ~ namespace ssc::crypto_impl::dragonfly_v1 */
#undef UNLOCK_MEMORY_
#undef LOCK_MEMORY_
