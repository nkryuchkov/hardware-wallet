/*
 * This file is part of the Skycoin project, https://skycoin.net/
 *
 * Copyright (C) 2018-2019 Skycoin Project
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include "skycoin_check_signature.h"

#include "curves.h"
#include "secp256k1.h"
#include "ecdsa.h"
#include <string.h> // memcpy
// #include "bignum.h"

// Compute uncompressed public key from compact signature.
// Returns 0 if verification succeeded
int verify_digest_recover(uint8_t* pub_key, const uint8_t* sig, const uint8_t* digest)
{
	const ecdsa_curve* curve = get_curve_by_name(SECP256K1_NAME)->params;

    bignum256 r, s, e;
    curve_point cp, cp2;

    // read r and s
    bn_read_be(sig, &r);
    bn_read_be(sig + 32, &s);
    if (!bn_is_less(&r, &curve->order) || bn_is_zero(&r)) {
        return 1;
    }
    if (!bn_is_less(&s, &curve->order) || bn_is_zero(&s)) {
        return 1;
    }
    uint8_t recid = sig[64];

    /*
    SKYCOIN CIPHER AUDIT
	Compare to function: Signature.Recover
    */
    // cp = R = k * G (k is secret nonce when signing)
    if (recid & 2) {
        bn_add(&r, &curve->order);
        if (!bn_is_less(&r, &curve->prime)) {
            return 1;
        }
    }

    memcpy(&cp.x, &r, sizeof(bignum256));

    // compute y from x
    uncompress_coords(curve, recid & 1, &cp.x, &cp.y);
    if (!ecdsa_validate_pubkey(curve, &cp)) {
        return 1;
    }
    // r := r^-1
    bn_inverse(&r, &curve->order);

    // e = -digest
    bn_read_be(digest, &e);
    while (!(uint8_t)e.val[0]) {
        for (int i = 0; i < 8; ++i) {
            bn_rshift(&e);
        }
    }

    bn_multiply(&r, &e, &curve->order);
    bn_subtractmod(&curve->order, &e, &e, &curve->order);
    bn_fast_mod(&e, &curve->order);
    bn_mod(&e, &curve->order);

    bn_multiply(&r, &s, &curve->order);

    // cp := s * R = s * k *G
    point_multiply(curve, &s, &cp, &cp);
    // cp2 := -digest * G
    scalar_multiply(curve, &e, &cp2);

    // cp := (s * k - digest) * G = (r*priv) * G = r * Pub
    point_add(curve, &cp2, &cp);
    pub_key[0] = 0x04;
    bn_write_be(&cp.x, pub_key + 1);
    bn_write_be(&cp.y, pub_key + 33);

    return 0;
}

/*signature: 65 bytes,
message 32 bytes,
pubkey 33 bytes
returns 0 if signature matches and 5 if it does not*/
int recover_pubkey_from_signed_message(const char* message, const uint8_t* signature, uint8_t* pubkey)
{
    int res = -1;
    uint8_t long_pubkey[65];

    res = verify_digest_recover(long_pubkey, signature, (uint8_t*)message);
    memcpy(&pubkey[1], &long_pubkey[1], 32);
    if (long_pubkey[64] % 2 == 0) {
        pubkey[0] = 0x02;
    } else {
        pubkey[0] = 0x03;
    }
    return res;
}
