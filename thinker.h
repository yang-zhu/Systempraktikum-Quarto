#ifndef THINKER_H
#define THINKER_H

typedef struct {
    int *board;
    int givenPiece;
    bool *pieces;
    int depth;
    int alpha;
    int beta;
    int *selectedPos;
    int *selectedPiece;
    int *prediction;
} negamaxParam;

void signalHandlerThinker(int signalKey);
void think(void);
bool isImmediateWin(int *selectedPos, int *board, int piece);
void *computeMove(void *voidParams);
int negamax(int *board, int givenPiece, bool pieces[16], int depth, int alpha, int beta, int *selectedPos, int *selectedPiece);
void thinkerCleanUp(void);

#endif