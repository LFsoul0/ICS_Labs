/* 
 * trans.c - Matrix transpose B = A^T
 *
 * Name: Wang Tingyu
 * ID: 519021910475
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */ 
#include <stdio.h>
#include "cachelab.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/* 
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded. 
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
    /* local variables */
    int x0, x1, x2, x3, x4, x5, x6, x7;	/* 8 tmp data */
    int i, j, k, l; 			/* 4 loop variables (or swap medium) */ 

    /* case 32 * 32 */
    if (M == 32 && N == 32) {
	for (i = 0; i < 32; i += 8) {		/* split to 8 * 8 blocks */
	    for (j = 0; j < 32; j += 8) {	
		for (k = i; k < i + 8; ++k) {	/* for every row in block */
		    /* read a row */
		    x0 = A[k][j];
		    x1 = A[k][j + 1];
		    x2 = A[k][j + 2];
		    x3 = A[k][j + 3];
		    x4 = A[k][j + 4];
		    x5 = A[k][j + 5];
		    x6 = A[k][j + 6];
		    x7 = A[k][j + 7];

		    /* write a column */
		    B[j][k] = x0;
		    B[j + 1][k] = x1;
		    B[j + 2][k] = x2;
		    B[j + 3][k] = x3;
		    B[j + 4][k] = x4;
		    B[j + 5][k] = x5;
		    B[j + 6][k] = x6;
		    B[j + 7][k] = x7;
		}
	    }
	}
    }

    /* case 64 * 64 */
    else if (M == 64 && N == 64) {
	for (i = 0; i < 64; i += 8) {		/* split to 8 * 8 blocks */
	    for (j = 0; j < 64; j += 8) {
		/* move first 4 rows in A to first 4 rows in B */
		for (k = i; k < i + 4; ++k) {	
		    /* read a row */
		    x0 = A[k][j];
		    x1 = A[k][j + 1];
		    x2 = A[k][j + 2];
		    x3 = A[k][j + 3];
		    x4 = A[k][j + 4];
		    x5 = A[k][j + 5];
		    x6 = A[k][j + 6];
		    x7 = A[k][j + 7];

		    /* write half columns to avoid eviction */
		    B[j][k] = x0;
		    B[j + 1][k] = x1;
		    B[j + 2][k] = x2;
		    B[j + 3][k] = x3;
		
		    /* store rest part in column k + 4 */
		    B[j][k + 4] = x4;
		    B[j + 1][k + 4] = x5;
		    B[j + 2][k + 4] = x6;
		    B[j + 3][k + 4] = x7;
		}

		/* move last 4 row by column and correct misplaced data */
		for (k = j; k < j + 4; ++k) {
		    /* read column k */
		    x0 = A[i + 4][k];
		    x1 = A[i + 5][k];
		    x2 = A[i + 6][k];
		    x3 = A[i + 7][k];

		    /* read column k + 4 */
		    x4 = A[i + 4][k + 4];
		    x5 = A[i + 5][k + 4];
		    x6 = A[i + 6][k + 4];
		    x7 = A[i + 7][k + 4];

		    /* swap x0~x3 with row k in B */
		    l = B[k][i + 4]; B[k][i + 4] = x0; x0 = l;
		    l = B[k][i + 5]; B[k][i + 5] = x1; x1 = l;
		    l = B[k][i + 6]; B[k][i + 6] = x2; x2 = l;
		    l = B[k][i + 7]; B[k][i + 7] = x3; x3 = l;

		    /* write x0~x7 to row k + 4 */
		    B[k + 4][i] = x0;
		    B[k + 4][i + 1] = x1;
		    B[k + 4][i + 2] = x2;
		    B[k + 4][i + 3] = x3;
		    B[k + 4][i + 4] = x4;
		    B[k + 4][i + 5] = x5;
		    B[k + 4][i + 6] = x6;
		    B[k + 4][i + 7] = x7;
		}
	    }
	}
    }

    /* case 61 * 67 or else */
    else {
	for (i = 0; i < N; i += 16) {			/* split to 16 * 16 blocks */
	    for (j = 0; j < M; j += 16) {
		for (k = i; k < i + 16 && k < N; ++k) {	/* for every element in block */
		    for (l = j; l < j + 16 && l < M; ++l) {
			B[l][k] = A[k][l];
		    }
		}
	    }
	}
    }
}

/* 
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started. 
 */ 

/* 
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, tmp;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; j++) {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }    

}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc); 

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc); 

}

/* 
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}

