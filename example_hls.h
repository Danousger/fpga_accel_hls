#define MAT_A_ROWS 32
#define MAT_A_COLS 32
#define MAT_B_ROWS 32
#define MAT_B_COLS 32

typedef int mat_a_t;
typedef int mat_b_t;
typedef int result_t;

void matrixmul(
      mat_a_t a[MAT_A_ROWS][MAT_A_COLS],
      mat_b_t b[MAT_B_ROWS][MAT_B_COLS],
      result_t res[MAT_A_ROWS][MAT_B_COLS]);
