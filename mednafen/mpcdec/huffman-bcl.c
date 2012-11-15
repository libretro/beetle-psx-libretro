/*************************************************************************
* Huffman codes generation, part of the code from the Basic Compression
* Library ( http://bcl.sourceforge.net )
*
* Modified by Nicolas BOTTI rududu at laposte.net
*
*-------------------------------------------------------------------------
* Copyright (c) 2003-2006 Marcus Geelnard
*
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any damages
* arising from the use of this software.
*
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
*
* 1. The origin of this software must not be misrepresented; you must not
*    claim that you wrote the original software. If you use this software
*    in a product, an acknowledgment in the product documentation would
*    be appreciated but is not required.
*
* 2. Altered source versions must be plainly marked as such, and must not
*    be misrepresented as being the original software.
*
* 3. This notice may not be removed or altered from any source
*    distribution.
*
* Marcus Geelnard
* marcus.geelnard at home.se
*************************************************************************/

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int Symbol;
    unsigned int Count;
    unsigned int Code;
    unsigned int Bits;
} huff_sym_t;

typedef struct huff_node huff_node_t;

struct huff_node {
	huff_node_t * ChildA;
	union {
		huff_node_t * ChildB;
		huff_sym_t * Symbol;
	};
    int Count;
};

static void _Huffman_StoreTree( huff_node_t *node, unsigned int bits )
{
	/* Is this a leaf node? */
	if( node->ChildA == 0 ) {
		/* Store code info in symbol array */
		node->Symbol->Bits = bits;
		return;
	}

	/* Branch A */
	_Huffman_StoreTree( node->ChildA, bits+1 );

	/* Branch B */
	_Huffman_StoreTree( node->ChildB, bits+1 );
}

/**
 * Compare 2 symbols to sort as canonical huffman (more bits first)
 * @param sym1
 * @param sym2
 * @return
 */
static int _Huffman_CompBits(const huff_sym_t * sym1, const huff_sym_t * sym2)
{
	if (sym1->Bits == sym2->Bits){
		if (sym1->Symbol == sym2->Symbol)
			return 0;
		else
			return ((sym1->Symbol > sym2->Symbol) << 1) - 1;
	} else
		return ((sym1->Bits < sym2->Bits) << 1) - 1;
}

/**
 * Compare 2 symbols to sort in symbol order
 * @param sym1
 * @param sym2
 * @return
 */
static int _Huffman_CompSym(const huff_sym_t * sym1, const huff_sym_t * sym2)
{
	return ((sym1->Symbol > sym2->Symbol) << 1) - 1;
}

/**
 * Generate canonical huffman codes from symbols and bit lengths
 * @param sym
 * @param num_symbols
 */
static void _Huffman_MakeCodes(huff_sym_t * sym, unsigned int num_symbols)
{
	unsigned int code = 0, i;
	int bits;

	qsort(sym, num_symbols, sizeof(huff_sym_t),
		  (int (*)(const void *, const void *)) _Huffman_CompBits);

	bits = sym[0].Bits;
	sym[0].Code = 0;

	for( i = 1; i < num_symbols; i++){
		code >>= bits - sym[i].Bits;
		bits = sym[i].Bits;
		code++;
		sym[i].Code = code;
	}
}


/**
 * Make a canonical huffman tree from symbols and counts
 * @param sym
 * @param num_symbols
 */
void _Huffman_MakeTree( huff_sym_t * sym, unsigned int num_symbols)
{
	huff_node_t * nodes, * node_1, * node_2, * root;
	unsigned int k, nodes_left, next_idx;

	nodes = malloc(sizeof(huff_node_t) * (num_symbols * 2 - 1));

	/* Initialize all leaf nodes */
	for( k = 0; k < num_symbols; ++ k ) {
		nodes[k].Symbol = & sym[k];
		nodes[k].Count = sym[k].Count;
		nodes[k].ChildA = (huff_node_t *) 0;
	}

	/* Build tree by joining the lightest nodes until there is only
		one node left (the root node). */
	root = (huff_node_t *) 0;
	nodes_left = num_symbols;
	next_idx = num_symbols;
	while( nodes_left > 1 )	{
		/* Find the two lightest nodes */
		node_1 = (huff_node_t *) 0;
		node_2 = (huff_node_t *) 0;
		for( k = 0; k < next_idx; ++ k ) {
			if( nodes[k].Count >= 0 ) {
				if( !node_1 || (nodes[k].Count <= node_1->Count) ) {
					node_2 = node_1;
					node_1 = &nodes[k];
				} else if( !node_2 || (nodes[k].Count <= node_2->Count) )
					node_2 = &nodes[k];
			}
		}

		/* Join the two nodes into a new parent node */
		root = &nodes[next_idx];
		root->ChildA = node_1;
		root->ChildB = node_2;
		root->Count = node_1->Count + node_2->Count;
		node_1->Count = -1;
		node_2->Count = -1;
		++ next_idx;
		-- nodes_left;
	}

	/* Store the tree in the output stream, and in the sym[] array (the
		latter is used as a look-up-table for faster encoding) */
	if( root ) {
		_Huffman_StoreTree( root, 0 );
	} else {
		/* Special case: only one symbol => no binary tree */
		root = &nodes[0];
		_Huffman_StoreTree( root, 1 );
	}

	free(nodes);

	_Huffman_MakeCodes(sym, num_symbols);
}
