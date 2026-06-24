/* Minimal raw-TCP HTTP/1.1 server for the Nintendo WFC + Gen 4 GTS endpoints. */
#ifndef PICO_BALL_WFC_HTTP_H
#define PICO_BALL_WFC_HTTP_H

/* Bind on TCP :80 and start serving. Returns 0 on success. */
int wfc_http_init(void);

#endif /* PICO_BALL_WFC_HTTP_H */
