// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <algorithm>
#include <stdexcept>
#include "seal/encryptor.h"
#include "seal/randomgen.h"
#include "seal/randomtostd.h"
#include "seal/smallmodulus.h"
#include "seal/util/common.h"
#include "seal/util/uintarith.h"
#include "seal/util/polyarithsmallmod.h"
#include "seal/util/clipnormal.h"
#include "seal/util/smallntt.h"
#include "seal/util/rlwe.h"
#include "seal/util/scalingvariant.h"

using namespace std;

namespace seal
{
    Encryptor::Encryptor(shared_ptr<SEALContext> context,
        const PublicKey &public_key) : context_(move(context))
    {
        // Verify parameters
        if (!context_)
        {
            throw invalid_argument("invalid context");
        }
        if (!context_->parameters_set())
        {
            throw invalid_argument("encryption parameters are not set correctly");
        }

        // Verify and set public_key
        if (public_key.parms_id() != context_->key_parms_id())
        {
            throw invalid_argument("public key is not valid for encryption parameters");
        }
        public_key_ = public_key;

        auto &parms = context_->key_context_data()->parms();
        auto &coeff_modulus = parms.coeff_modulus();
        size_t coeff_count = parms.poly_modulus_degree();
        size_t coeff_mod_count = coeff_modulus.size();

        // Quick sanity check
        if (!util::product_fits_in(coeff_count, coeff_mod_count, size_t(2)))
        {
            throw logic_error("invalid parameters");
        }
    }

    Encryptor::Encryptor(shared_ptr<SEALContext> context,
        const SecretKey &secret_key) : context_(move(context))
    {
        // Verify parameters
        if (!context_)
        {
            throw invalid_argument("invalid context");
        }
        if (!context_->parameters_set())
        {
            throw invalid_argument("encryption parameters are not set correctly");
        }

        // Verify and set secret_key
        if (secret_key.parms_id() != context_->key_parms_id())
        {
            throw invalid_argument("secret key is not valid for encryption parameters");
        }
        secret_key_ = secret_key;

        auto &parms = context_->key_context_data()->parms();
        auto &coeff_modulus = parms.coeff_modulus();
        size_t coeff_count = parms.poly_modulus_degree();
        size_t coeff_mod_count = coeff_modulus.size();

        // Quick sanity check
        if (!util::product_fits_in(coeff_count, coeff_mod_count, size_t(2)))
        {
            throw logic_error("invalid parameters");
        }
    }

    Encryptor::Encryptor(shared_ptr<SEALContext> context,
        const PublicKey &public_key, const SecretKey &secret_key)
        : context_(move(context))
    {
        // Verify parameters
        if (!context_)
        {
            throw invalid_argument("invalid context");
        }
        if (!context_->parameters_set())
        {
            throw invalid_argument("encryption parameters are not set correctly");
        }

        // Verify and set public_key
        if (public_key.parms_id() != context_->key_parms_id())
        {
            throw invalid_argument("public key is not valid for encryption parameters");
        }
        public_key_ = public_key;

        // Verify and set secret_key
        if (secret_key.parms_id() != context_->key_parms_id())
        {
            throw invalid_argument("secret key is not valid for encryption parameters");
        }
        secret_key_ = secret_key;

        auto &parms = context_->key_context_data()->parms();
        auto &coeff_modulus = parms.coeff_modulus();
        size_t coeff_count = parms.poly_modulus_degree();
        size_t coeff_mod_count = coeff_modulus.size();

        // Quick sanity check
        if (!util::product_fits_in(coeff_count, coeff_mod_count, size_t(2)))
        {
            throw logic_error("invalid parameters");
        }
    }

    void Encryptor::encrypt_zero_custom(parms_id_type parms_id, Ciphertext &destination,
        bool is_asymmetric, bool save_seed, MemoryPoolHandle pool)
    {
        // Verify parameters.
        if (!pool)
        {
            throw invalid_argument("pool is uninitialized");
        }
        auto context_data_ptr = context_->get_context_data(parms_id);
        if (!context_data_ptr)
        {
            throw invalid_argument("parms_id is not valid for encryption parameters");
        }
        auto &context_data = *context_->get_context_data(parms_id);
        auto &parms = context_data.parms();
        size_t coeff_mod_count = parms.coeff_modulus().size();
        size_t coeff_count = parms.poly_modulus_degree();
        bool is_ntt_form = false;
        if (parms.scheme() == scheme_type::CKKS)
        {
            is_ntt_form = true;
        }
        else if (parms.scheme() != scheme_type::BFV)
        {
            throw invalid_argument("unsupported scheme");
        }

        // Resize destination and save results
        destination.resize(context_, parms_id, 2);

        // If asymmetric key encryption
        if (is_asymmetric)
        {
            // Verify public_key
            if (public_key_.parms_id() != context_->key_parms_id())
            {
                throw logic_error("public key is not valid for encryption parameters");
            }

            auto prev_context_data_ptr = context_data.prev_context_data();
            // Requires modulus switching
            if (prev_context_data_ptr)
            {
                auto &prev_context_data = *prev_context_data_ptr;
                auto &prev_parms_id = prev_context_data.parms_id();
                auto &base_converter = prev_context_data.base_converter();

                // Zero encryption without modulus switching
                Ciphertext temp(pool);
                util::encrypt_zero_asymmetric(public_key_, context_, prev_parms_id,
                    is_ntt_form, temp, pool);

                // Modulus switching
                for (size_t j = 0; j < 2; j++)
                {
                    if (is_ntt_form)
                    {
                        base_converter->round_last_coeff_modulus_ntt_inplace(
                            temp.data(j),
                            prev_context_data.small_ntt_tables(),
                            pool);
                    }
                    else
                    {
                        base_converter->round_last_coeff_modulus_inplace(
                            temp.data(j),
                            pool);
                    }
                    util::set_poly_poly(
                        temp.data(j),
                        coeff_count,
                        coeff_mod_count,
                        destination.data(j));
                }
                destination.is_ntt_form() = is_ntt_form;
                destination.scale() = temp.scale();
            }
            // Does not require modulus switching
            else
            {
                util::encrypt_zero_asymmetric(public_key_, context_, parms_id,
                    is_ntt_form, destination, pool);
            }
        }
        else
        {
            // Verify secret_key
            if (secret_key_.parms_id() != context_->key_parms_id())
            {
                throw logic_error("secret key is not valid for encryption parameters");
            }
            util::encrypt_zero_symmetric(secret_key_, context_, parms_id,
                is_ntt_form, destination, pool, save_seed);
            // Does not require modulus switching
        }
    }

    void Encryptor::encrypt_custom(const Plaintext &plain, Ciphertext &destination,
        bool is_asymmetric, bool save_seed, MemoryPoolHandle pool)
    {
        // Verify that plain is valid.
        if (!is_valid_for(plain, context_))
        {
            throw invalid_argument("plain is not valid for encryption parameters");
        }
        auto scheme = context_->key_context_data()->parms().scheme();
        if (scheme == scheme_type::BFV)
        {
            if (plain.is_ntt_form())
            {
                throw invalid_argument("plain cannot be in NTT form");
            }
            encrypt_zero_custom(context_->first_parms_id(), destination,
                is_asymmetric, save_seed, pool);
            
            // Multiply plain by scalar coeff_div_plaintext and reposition if in upper-half.
            // Result gets added into the c_0 term of ciphertext (c_0,c_1).
            util::multiply_add_plain_with_scaling_variant(
                plain, *context_->first_context_data(), destination.data());
        }
        else if (scheme == scheme_type::CKKS)
        {
            if (!plain.is_ntt_form())
            {
                throw invalid_argument("plain must be in NTT form");
            }
            auto context_data_ptr = context_->get_context_data(plain.parms_id());
            if (!context_data_ptr)
            {
                throw invalid_argument("plain is not valid for encryption parameters");
            }
            encrypt_zero_custom(plain.parms_id(), destination,
                is_asymmetric, save_seed, pool);

            auto &parms = context_->get_context_data(plain.parms_id())->parms();
            auto &coeff_modulus = parms.coeff_modulus();
            size_t coeff_mod_count = coeff_modulus.size();
            size_t coeff_count = parms.poly_modulus_degree();
            // The plaintext gets added into the c_0 term of ciphertext (c_0,c_1).
            for (size_t i = 0; i < coeff_mod_count; i++)
            {
                util::add_poly_poly_coeffmod(
                    destination.data() + (i * coeff_count),
                    plain.data() + (i * coeff_count),
                    coeff_count,
                    coeff_modulus[i],
                    destination.data() + (i * coeff_count));
            }
            destination.scale() = plain.scale();
        }
        else
        {
            throw invalid_argument("unsupported scheme");
        }
    }
}
