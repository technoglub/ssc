#pragma once
#include <ssc/crypto/threefish.hh>
#include <ssc/crypto/ubi.hh>
#include <ssc/general/integers.hh>
#include <ssc/general/symbols.hh>


namespace ssc
{
    template <std::size_t State_Bits>
    class Skein
    {
    public:
        /* PUBLIC CONSTANTS AND COMPILE-TIME CHECKS */
        static_assert((State_Bits ==  256 ||
                       State_Bits ==  512 ||
                       State_Bits == 1024),
                      "Skein is only defined for 256, 512, 1024 bit states.");
        using UBI_t = UBI<Threefish<State_Bits>, State_Bits>;
        using Type_Mask_t = typename UBI_t::Type_Mask_t;
        static constexpr const std::size_t State_Bytes = State_Bits / 8;
        
        /* PUBLIC INTERFACE */
        void hash(u8_t * const bytes_out,
                  u8_t const * const bytes_in,
                  u64_t const num_bytes_in,
                  u64_t const num_bytes_out = State_Bytes);
        void MAC(u8_t * const bytes_out,
                 u8_t const * const bytes_in,
                 u8_t const * const key_in,
                 u64_t const num_bytes_in,
                 u64_t const num_key_bytes_in,
                 u64_t const num_bytes_out = State_Bytes);
        void hash_native(u8_t * const bytes_out,
                         u8_t const * const bytes_in,
                         u64_t const num_bytes_in);
    private:
        /* PRIVATE DATA */
        UBI_t ubi;
        /* PRIVATE INTERFACE */
        void process_config_block_ (u64_t const num_output_bits);
        void process_key_block_    (u8_t const * const key_in,
                                           u64_t const key_size);
        void process_message_block_(u8_t const * const message_in,
                                           u64_t const message_size);
        void output_transform_     (u8_t * const out,
                                     u64_t const num_output_bytes);
    };
    
    template <std::size_t State_Bits>
    void Skein<State_Bits>::process_config_block_(u64_t const num_output_bits)
    {
        /* Setup configuration string */
        u8_t config[32] = {
            // first 4 bytes
            0x53, 0x48, 0x41, 0x33, // schema identifier "SHA3"
            // next 2 bytes
            0x01, 0x00,             // version number (1)
            // next 2 bytes
            0x00, 0x00,             // reserved (0)
            // next 8 bytes
            0x00, 0x00, 0x00, 0x00, // output length
            0x00, 0x00, 0x00, 0x00,
            // remaining 16 bytes
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        };
        *reinterpret_cast<u64_t *>(config + 8) = num_output_bits;
        ubi.chain( Type_Mask_t::T_cfg, config, sizeof(config) );
    }
    
    template <std::size_t State_Bits>
    void Skein<State_Bits>::process_key_block_(u8_t const * const key_in,
                                               u64_t const        key_size)
    {
        ubi.chain( Type_Mask_t::T_key, key_in, key_size );
    }
    
    template <std::size_t State_Bits>
    void Skein<State_Bits>::process_message_block_(u8_t const * const message_in,
                                                   u64_t const        message_size)
    {
        ubi.chain( Type_Mask_t::T_msg, message_in, message_size );
    }
    
    template <std::size_t State_Bits>
    void Skein<State_Bits>::output_transform_(u8_t * const out,
                                              u64_t const  num_output_bytes)
    {
        u8_t * bytes_out = out;
        u64_t number_iterations = num_output_bytes / State_Bytes;
        if ( (num_output_bytes % State_Bytes) != 0 )
        {
            ++number_iterations;
        }
        u64_t bytes_left = num_output_bytes;
        for ( u64_t i = 0; i < number_iterations; ++i )
        {
            ubi.chain( Type_Mask_t::T_out,
                         reinterpret_cast<u8_t *>(&i),
                         sizeof(i) );
            if ( bytes_left >= State_Bytes )
            {
                std::memcpy( bytes_out, ubi.get_key_state(), State_Bytes );
                bytes_out  += State_Bytes;
                bytes_left -= State_Bytes;
            }
            else
            {
                std::memcpy( bytes_out, ubi.get_key_state(), bytes_left );
                break;
            }
        }
    }/* ! output_transform(...) */
    
    template <std::size_t State_Bits>
    void Skein<State_Bits>::hash(u8_t * const       bytes_out,
                                 u8_t const * const bytes_in,
                                 u64_t const        num_bytes_in,
                                 u64_t const        num_bytes_out)
    {
        ubi.clear_key_state();
        static_assert(CHAR_BIT == 8);
        process_config_block_( num_bytes_out * CHAR_BIT );
        process_message_block_( bytes_in, num_bytes_in );
        output_transform_( bytes_out, num_bytes_out );
    }
    
    template <std::size_t State_Bits>
    void Skein<State_Bits>::MAC(u8_t * const       bytes_out,
                                u8_t const * const bytes_in,
                                u8_t const * const key_in,
                                u64_t const        num_bytes_in,
                                u64_t const        num_key_bytes_in,
                                u64_t const        num_bytes_out)
    {
        ubi.clear_key_state();
        process_key_block_( key_in, num_key_bytes_in );
        static_assert(CHAR_BIT == 8);
        process_config_block_( num_bytes_out * CHAR_BIT );
        process_message_block_( bytes_in, num_bytes_in );
        output_transform_( bytes_out, num_bytes_out );
    }
    
    template <std::size_t State_Bits>
    void Skein<State_Bits>::hash_native(u8_t * const       bytes_out,
                                        u8_t const * const bytes_in,
                                        u64_t const        num_bytes_in)
    {
        static_assert(State_Bits == 256 ||
                      State_Bits == 512 ||
                      State_Bits == 1024,
                      "Skein is only defined for 256, 512, 1024 bit-widths");
        if constexpr(State_Bits == 256)
        {
            static constexpr u64_t const init_chain [4] = {
                0xfc9d'a860'd048'b449,
                0x2fca'6647'9fa7'd833,
                0xb33b'c389'6656'840f,
                0x6a54'e920'fde8'da69
            };
            std::memcpy( ubi.get_key_state(), init_chain, sizeof(init_chain) );
        }
        else if constexpr(State_Bits == 512)
        {
            static constexpr u64_t const init_chain[8] = {
                0x4903'adff'749c'51ce,
                0x0d95'de39'9746'df03,
                0x8fd1'9341'27c7'9bce,
                0x9a25'5629'ff35'2cb1,
                0x5db6'2599'df6c'a7b0,
                0xeabe'394c'a9d5'c3f4,
                0x9911'12c7'1a75'b523,
                0xae18'a40b'660f'cc33
            };
            std::memcpy( ubi.get_key_state(), init_chain, sizeof(init_chain) );
        }
        else if constexpr(State_Bits == 1024)
        {
            static constexpr u64_t const init_chain[16] = {
                0xd593'da07'41e7'2355, // 0
                0x15b5'e511'ac73'e00c, // 1
                0x5180'e5ae'baf2'c4f0, // 2
                0x03bd'41d3'fcbc'afaf, // 3
                0x1cae'c6fd'1983'a898, // 4
                0x6e51'0b8b'cdd0'589f, // 5
                0x77e2'bdfd'c639'4ada, // 6
                0xc11e'1db5'24dc'b0a3, // 7
                0xd6d1'4af9'c632'9ab5, // 8
                0x6a9b'0bfc'6eb6'7e0d, // 9
                0x9243'c60d'ccff'1332, //10
                0x1a1f'1dde'743f'02d4, //11
                0x0996'753c'10ed'0bb8, //12
                0x6572'dd22'f2b4'969a, //13
                0x61fd'3062'd00a'579a, //14
                0x1de0'536e'8682'e539  //15
            };
            std::memcpy( ubi.get_key_state(), init_chain, sizeof(init_chain) );
        }
        process_message_block_( bytes_in, num_bytes_in );
        output_transform_( bytes_out, State_Bytes );
    }
#if defined( _WIN32 )
    template class DLL_PUBLIC Skein<512>;
#endif
}/* ! namespace ssc */
    
