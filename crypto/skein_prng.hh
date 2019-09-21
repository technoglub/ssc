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
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>
#include <ssc/crypto/skein.hh>
#include <ssc/crypto/operations.hh>
#include <ssc/general/integers.hh>
#include <ssc/general/symbols.hh>

namespace ssc {
        template <std::size_t State_Bits>
        class Skein_PRNG {
        public:
                static_assert (State_Bits == 256 ||
                               State_Bits == 512 ||
                               State_Bits == 1024,
                               "Skein_PRNG only defined for state sizes of 256,512,1024 bits");
                static constexpr const size_t State_Bytes = State_Bits / 8;
                using Skein_t = Skein<State_Bits>;

                Skein_PRNG (void)
                Skein_PRNG (u8_t const * const seed,
                            u64_t const        seed_bytes);
                ~Skein_PRNG (void);


                /* void reseed(seed,seed_bytes)
                 *      Copies in ${seed_bytes} bytes into the state, and
                 *      hashes them. */
                void
                reseed (u8_t const * const seed,
                        u64_t const        seed_bytes);

                /* void os_reseed(seed_bytes)
                 *      Reseeds the state using ${seed_bytes} bytes of entropy
                 *      received from the operating system. */
                void
                os_reseed (u64_t const seed_bytes);

                /* void get(output_buffer,requested_bytes)
                 *      Writes ${requested_bytes} pseudorandom bytes into the ${output_buffer}. */
                void
                get (u8_t * const output_buffer,
                     u64_t const  requested_bytes);
        private:
                u8_t    state [State_Bytes];
                Skein_t skein;
        }; /* ! class Skein_PRNG */

        template<std::size_t State_Bits>
        Skein_PRNG<State_Bits>::Skein_PRNG (void) {
                this->os_reseed( sizeof(state) );
        } /* Skein_PRNG (void) */

        template <std::size_t State_Bits>
        Skein_PRNG<State_Bits>::Skein_PRNG (u8_t const * const seed,
                                            u64_t const        seed_bytes) {
                this->reseed( seed, seed_bytes );
        } /* Skein_PRNG (u8_t*,u64_t) */

        template <std::size_t State_Bits>
        Skein_PRNG<State_Bits>::~Skein_PRNG (void) {
                // Securely zero the buffer on destruction.
                zero_sensitive( state, sizeof(state) );
        } /* ~Skein_PRNG */

        template <std::size_t State_Bits>
        void
        Skein_PRNG<State_Bits>::reseed (u8_t const * const seed,
                                        u64_t const        seed_bytes) {
                // Enough bytes for the current state and new bytes.
                u64_t const buffer_size = sizeof(state) + seed_bytes;   
                auto buffer = std::make_unique<u8_t[]>( buffer_size );
                // Copy the state into the beginning of the temp buffer.
                std::memcpy( buffer.get(), state, sizeof(state) );
                // Copy the seed into the buffer immediately after the end of the copied-in state.
                std::memcpy( (buffer.get() + sizeof(state)), seed, seed_bytes );
                // Hash the buffer, outputting the hash back into the state.
                skein.hash_native( state, buffer.get(), buffer_size );
                // Securely zero over the used buffer.
                zero_sensitive( buffer.get(), buffer_size );
        } /* reseed (u8_t*,u64_t) */

        template <std::size_t State_Bits>
        void
        Skein_PRNG<State_Bits>::os_reseed (u64_t const seed_bytes) {
                // Enough bytes for the current state and seed bytes.
                u64_t const buffer_size = sizeof(state) + seed_bytes;
                auto buffer = std::make_unique<u8_t[]>( buffer_size );
                // Copy the state into the beginning of the temp buffer.
                std::memcpy( buffer.get(), state, sizeof(state) );
                // Write ${seed_bytes} bytes of entropy after the end of the copied-in state.
                generate_random_bytes( buffer.get() + sizeof(state), seed_bytes );
                // Hash the buffer, outputting the hash back into the state.
                skein.hash_native( state, buffer.get(), buffer_size );
                // Securely zero over the used buffer.
                zero_sensitive( buffer.get(), buffer_size );
        } /* os_reseed (u64_t) */

        template <std::size_t State_Bits>
        void
        Skein_PRNG<State_Bits>::get (u8_t * const output_buffer,
                                     u64_t const  requested_bytes) {
                // Enough bytes for the current state and requested bytes.
                u64_t const buffer_size = sizeof(state) + requested_bytes;
                auto buffer = std::make_unique<u8_t[]>( buffer_size );
                // Hash the state, writing ${buffer_size} bytes of pseudorandomness to ${buffer.get()}
                skein.hash( buffer.get(), state, sizeof(state), buffer_size );
                // Copy sizeof(state) bytes into the state.
                std::memcpy( state, buffer.get(), sizeof(state) );
                // Copy the remaining ${requested_bytes} into the output buffer.
                std::memcpy( output_buffer, (buffer.get() + sizeof(state)), requested_bytes );
                // Securely zero over the used buffer.
                zero_sensitive( buffer.get(), buffer_size );
        } /* get (u8_t*,u64_t) */
}/* ! namespace ssc */
