#include <ssc/crypto/cbc.hh>
#include <ssc/crypto/threefish.hh>

namespace ssc
{
    // CONSTRUCTORS
    template<typename Block_Cipher_t, std::size_t Block_Bits>
        CBC<Block_Cipher_t,Block_Bits>::CBC(Block_Cipher_t &&blk_c) 
        : blk_cipher{ std::move( blk_c ) }
        {
        }
    // DESTRUCTORS
    template<typename Block_Cipher_t, std::size_t Block_Bits>
        CBC<Block_Cipher_t,Block_Bits>::~CBC()
        {
            zero_sensitive( state, sizeof(state) );
        }
    template<typename Block_Cipher_t, std::size_t Block_Bits>
        void CBC<Block_Cipher_t,Block_Bits>::manually_set_state(const u8_t * const __restrict state_bytes)
        {
            std::memcpy( state, state_bytes, sizeof(state) );
        }
    template<typename Block_Cipher_t, std::size_t Block_Bits>
        std::size_t CBC<Block_Cipher_t,Block_Bits>::apply_iso_iec_7816_padding_(u8_t *bytes, const std::size_t prepadding_size)
        {
            /* Here, bytes_to_add is pre-emptively decremented by 1, as padding
               at least one byte is necessary for this padding scheme. */
            using namespace std;
            const size_t bytes_to_add = (Block_Bytes - (prepadding_size % Block_Bytes) - 1);
            bytes[ prepadding_size ] = 0x80u; // The byte 0x80 precedes any null bytes (if any) that make up the padding.
            memset( (bytes + prepadding_size + 1), 0x00u, bytes_to_add );
            return prepadding_size + 1 + bytes_to_add;
        }
    template<typename Block_Cipher_t, std::size_t Block_Bits>
        std::size_t CBC<Block_Cipher_t,Block_Bits>::count_iso_iec_7816_padding_bytes_(const u8_t * const bytes, const std::size_t padded_size)
        {
            using namespace std;
            size_t count = 0;
            for ( size_t i = padded_size - 1; padded_size > 0; --i ) {
                ++count;
                if ( bytes[ i ] == 0x80 )
                    return count;
            }
            die_fputs( "Error: Invalid CBC Padding!\n" );
        }
    template<typename Block_Cipher_t, std::size_t Block_Bits>
        std::size_t CBC<Block_Cipher_t,Block_Bits>::calculate_padded_ciphertext_size_(const std::size_t unpadded_plaintext_size)
        {
            return unpadded_plaintext_size + (Block_Bytes - (unpadded_plaintext_size % Block_Bytes));
        }

    template<typename Block_Cipher_t, std::size_t Block_Bits>
        void CBC<Block_Cipher_t,Block_Bits>::encrypt_no_padding(const u8_t *bytes_in, u8_t *bytes_out, const std::size_t size_in, const u8_t * __restrict iv)
        {
            using std::memcpy;

            if ( iv != nullptr )
                memcpy( state, iv, sizeof(state) );
            if ( bytes_in != bytes_out )
                memcpy( bytes_out, bytes_in, size_in );
            const std::size_t last_block_offset = size_in - Block_Bytes;
            for ( std::size_t b_off = 0; b_off <= last_block_offset; b_off += Block_Bytes ) {
                u8_t *current_block = bytes_out + b_off;
                xor_block<Block_Bits>( current_block, state );
                blk_cipher.cipher( current_block, current_block );
                memcpy( state, current_block, sizeof(state) );
            }
        }
    template<typename Block_Cipher_t, std::size_t Block_Bits>
        std::size_t CBC<Block_Cipher_t,Block_Bits>::encrypt(const u8_t *bytes_in, u8_t *bytes_out, const std::size_t size_in, const u8_t * __restrict iv)
        {
            using std::memcpy;
            // If an IV was supplied, copy it into the state
            if ( iv != nullptr )
                memcpy( state, iv, sizeof(state) );
            std::size_t bytes_left = size_in;
            const u8_t * in  = bytes_in;
            u8_t * out = bytes_out;
            u8_t buffer [Block_Bytes];
            static_assert(sizeof(state)   == Block_Bytes);
            static_assert(sizeof(buffer)  == Block_Bytes);
            while ( bytes_left >= Block_Bytes ) {
                memcpy( buffer, in, Block_Bytes );
                xor_block<Block_Bits>( buffer, state );
                blk_cipher.cipher( buffer, buffer );
                memcpy( state, buffer, Block_Bytes );
                memcpy( out  , buffer, Block_Bytes );

                in         += Block_Bytes;
                out        += Block_Bytes;
                bytes_left -= Block_Bytes;
            }
            // Padding Plaintext before a final encrypt
            memcpy( buffer, in, bytes_left );
            buffer[ bytes_left ] = 0x80;
            memset( (buffer + bytes_left + 1), 0, (Block_Bytes - (bytes_left + 1)) );
            // Final encrypt
            xor_block<Block_Bits>( buffer, state );
            blk_cipher.cipher( buffer, buffer );
            memcpy( state, buffer, Block_Bytes );
            memcpy( out  , buffer, Block_Bytes );
            zero_sensitive( buffer, Block_Bytes );
            return calculate_padded_ciphertext_size_( size_in );
        }
    template<typename Block_Cipher_t, std::size_t Block_Bits>
        std::size_t CBC<Block_Cipher_t,Block_Bits>::decrypt(const u8_t *bytes_in, u8_t *bytes_out, const std::size_t size_in, const u8_t * __restrict iv)
        {
            using std::memcpy;

            if ( iv != nullptr )
                memcpy( state, iv, sizeof(state) );
            const std::size_t last_block_offset = (size_in >= Block_Bytes) ? (size_in - Block_Bytes) : 0;
            u8_t ciphertext[Block_Bytes];
            u8_t buffer    [Block_Bytes];
            static_assert(sizeof(state)    == Block_Bytes);
            static_assert(sizeof(ciphertext) == Block_Bytes);
            static_assert(sizeof(buffer)     == Block_Bytes);
            for ( std::size_t b_off = 0; b_off <= last_block_offset; b_off += Block_Bytes ) {
                const u8_t *block_in  = bytes_in  + b_off;
                u8_t       *block_out = bytes_out + b_off;
                memcpy( ciphertext, block_in, Block_Bytes );
                blk_cipher.inverse_cipher( ciphertext, buffer );
                xor_block<Block_Bits>( buffer, state );
                memcpy( block_out, buffer    , Block_Bytes );
                memcpy( state  , ciphertext, Block_Bytes );
            }
            zero_sensitive( buffer    , Block_Bytes );
            zero_sensitive( ciphertext, Block_Bytes  );
            return size_in - count_iso_iec_7816_padding_bytes_( bytes_out, size_in );
        }
    template<typename Block_Cipher_t, std::size_t Block_Bits>
        void CBC<Block_Cipher_t,Block_Bits>::decrypt_no_padding(const u8_t *bytes_in, u8_t *bytes_out, const std::size_t size_in, const u8_t * __restrict iv)
        {
            using std::memcpy;

            if ( iv != nullptr )
                memcpy( state, iv, sizeof(state) );
            const std::size_t last_block_offset = size_in - Block_Bytes;
            u8_t ciphertext[Block_Bytes];
            u8_t buffer    [Block_Bytes];
            static_assert(sizeof(state)    == Block_Bytes);
            static_assert(sizeof(ciphertext) == Block_Bytes);
            static_assert(sizeof(buffer)     == Block_Bytes);
            for ( std::size_t b_off = 0; b_off <= last_block_offset; b_off += Block_Bytes ) {
                const u8_t *block_in  = bytes_in  + b_off;
                u8_t       *block_out = bytes_out + b_off;
                memcpy( ciphertext, block_in, Block_Bytes );
                blk_cipher.inverse_cipher( ciphertext, buffer );
                xor_block<Block_Bits>( buffer, state );
                memcpy( block_out, buffer    , Block_Bytes );
                memcpy( state  , ciphertext, Block_Bytes );
            }
            zero_sensitive( buffer    , Block_Bytes );
            zero_sensitive( ciphertext, Block_Bytes );
        }
    template class DLL_PUBLIC CBC< Threefish<512>, 512 >;
}