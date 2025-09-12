#include "ErasureCodeJerasure.h"
#include "BufferList.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cassert>

extern "C" {
#include "jerasure.h"
#include "reed_sol.h"
#include "galois.h"
#include "cauchy.h"
#include "liberation.h"
}

#define LARGEST_VECTOR_WORDSIZE 16

using std::ostream;
using std::map;
using std::set;

int ErasureCodeJerasure::init(ErasureCodeProfile& profile, ostream *ss)
{
  int err = 0;
  std::cout << "technique=" << technique << std::endl;
  profile["technique"] = technique;
  err |= parse(profile, ss);
  if (err)
    return err;
  prepare();
  return ErasureCode::init(profile, ss);
}

int ErasureCodeJerasure::parse(ErasureCodeProfile &profile,
			       ostream *ss)
{
  int err = ErasureCode::parse(profile, ss);
  err |= to_int("k", profile, &k, DEFAULT_K, ss);
  err |= to_int("m", profile, &m, DEFAULT_M, ss);
  err |= to_int("w", profile, &w, DEFAULT_W, ss);
  if (chunk_mapping.size() > 0 && (int)chunk_mapping.size() != k + m) {
    *ss << "mapping " << profile.find("mapping")->second
	<< " maps " << chunk_mapping.size() << " chunks instead of"
	<< " the expected " << k + m << " and will be ignored" << std::endl;
    chunk_mapping.clear();
    err = -EINVAL;
  }
  err |= sanity_check_k_m(k, m, ss);
  return err;
}

unsigned int ErasureCodeJerasure::get_chunk_size(unsigned int stripe_width) const
{
  unsigned alignment = get_alignment();
  if (per_chunk_alignment) {
    unsigned chunk_size = stripe_width / k;
    if (stripe_width % k)
      chunk_size++;
    std::cout << "get_chunk_size: chunk_size " << chunk_size
	     << " must be modulo " << alignment; 
    assert(alignment <= chunk_size);
    unsigned modulo = chunk_size % alignment;
    if (modulo) {
      std::cout << "get_chunk_size: " << chunk_size
	       << " padded to " << chunk_size + alignment - modulo;
      chunk_size += alignment - modulo;
    }
    return chunk_size;
  } else {
    unsigned tail = stripe_width % alignment;
    unsigned padded_length = stripe_width + (tail ? (alignment - tail) : 0);
    assert(padded_length % k == 0);
    return padded_length / k;
  }
}

int ErasureCodeJerasure::encode_chunks(const set<int> &want_to_encode,
				       map<int, bufferlist> *encoded)
{
  char *chunks[k + m];
  for (int i = 0; i < k + m; i++)
    chunks[i] = (*encoded)[i].c_str();
  jerasure_encode(&chunks[0], &chunks[k], (*encoded)[0].length());
  return 0;
}

int ErasureCodeJerasure::decode_chunks(const set<int> &want_to_read,
				       const map<int, bufferlist> &chunks,
				       map<int, bufferlist> *decoded)
{
  unsigned blocksize = (*chunks.begin()).second.length();
  int erasures[k + m + 1];
  int erasures_count = 0;
  char *data[k];
  char *coding[m];
  for (int i =  0; i < k + m; i++) {
    if (chunks.find(i) == chunks.end()) {
      erasures[erasures_count] = i;
      erasures_count++;
    }
    if (i < k) {
      data[i] = (*decoded)[i].c_str();
    } else {
      coding[i - k] = (*decoded)[i].c_str();
    }
  }
  erasures[erasures_count] = -1;

  assert(erasures_count > 0);
  return jerasure_decode(erasures, data, coding, blocksize);
}

void ErasureCodeJerasure::encode_delta(const bufferptr &old_data,
                                       const bufferptr &new_data,
                                       bufferptr *delta_maybe_in_place)
{
  if (&old_data != delta_maybe_in_place) {
    memcpy(delta_maybe_in_place->c_str(), old_data.c_str(), delta_maybe_in_place->length());
  }
  char *new_data_p = const_cast<char*>(new_data.c_str());
  char *delta_p = delta_maybe_in_place->c_str();
  galois_region_xor(new_data_p, delta_p, delta_maybe_in_place->length());
}

bool ErasureCodeJerasure::is_prime(int value)
{
  int prime55[] = {
    2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,
    73,79,83,89,97,101,103,107,109,113,127,131,137,139,149,
    151,157,163,167,173,179,
    181,191,193,197,199,211,223,227,229,233,239,241,251,257
  };
  int i;
  for (i = 0; i < 55; i++)
    if (value == prime55[i])
      return true;
  return false;
}


void ErasureCodeJerasure::do_scheduled_ops(char **ptrs, int **operations, int packetsize, int s, int d)
{
  char *sptr;
  char *dptr;
  int op;

  for (op = 0; operations[op][0] >= 0; op++) {
    if (operations[op][0] == s && operations[op][2] == d) {
      sptr = ptrs[0] + operations[op][1]*packetsize;
      dptr = ptrs[1] + operations[op][3]*packetsize;
      galois_region_xor(sptr, dptr, packetsize);
    }
  }
}


//
// ErasureCodeJerasureReedSolomonVandermonde
//
void ErasureCodeJerasureReedSolomonVandermonde::jerasure_encode(char **data,
                                                                char **coding,
                                                                int blocksize)
{
  jerasure_matrix_encode(k, m, w, matrix, data, coding, blocksize);
}

int ErasureCodeJerasureReedSolomonVandermonde::jerasure_decode(int *erasures,
                                                                char **data,
                                                                char **coding,
                                                                int blocksize)
{
  return jerasure_matrix_decode(k, m, w, matrix, 1,
                                erasures, data, coding, blocksize);
}


unsigned ErasureCodeJerasureReedSolomonVandermonde::get_alignment() const
{
  if (per_chunk_alignment) {
    return w * LARGEST_VECTOR_WORDSIZE;
  } else {
    unsigned alignment = k*w*sizeof(int);
    if ( ((w*sizeof(int))%LARGEST_VECTOR_WORDSIZE) )
      alignment = k*w*LARGEST_VECTOR_WORDSIZE;
    return alignment;
  }
}

int ErasureCodeJerasureReedSolomonVandermonde::parse(ErasureCodeProfile &profile,
						     ostream *ss)
{
  int err = 0;
  err |= ErasureCodeJerasure::parse(profile, ss);
  if (w != 8 && w != 16 && w != 32) {
    *ss << "ReedSolomonVandermonde: w=" << w
	<< " must be one of {8, 16, 32} : revert to " << DEFAULT_W << std::endl;
    err = -EINVAL;
  }
  err |= to_bool("jerasure-per-chunk-alignment", profile,
		 &per_chunk_alignment, "false", ss);
  return err;
}

void ErasureCodeJerasureReedSolomonVandermonde::prepare()
{
  matrix = reed_sol_vandermonde_coding_matrix(k, m, w);
}

