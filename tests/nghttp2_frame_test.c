/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2012 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "nghttp2_frame_test.h"

#include <assert.h>
#include <stdio.h>

#include <CUnit/CUnit.h>

#include "nghttp2_frame.h"
#include "nghttp2_helper.h"
#include "nghttp2_test_helper.h"
#include "nghttp2_priority_spec.h"

static nghttp2_nv make_nv(const char *name, const char *value)
{
  nghttp2_nv nv;
  nv.name = (uint8_t*)name;
  nv.value = (uint8_t*)value;
  nv.namelen = strlen(name);
  nv.valuelen = strlen(value);
  nv.flags = NGHTTP2_NV_FLAG_NONE;

  return nv;
}

#define HEADERS_LENGTH 7

static nghttp2_nv* headers(void)
{
  nghttp2_nv *nva = malloc(sizeof(nghttp2_nv) * HEADERS_LENGTH);
  nva[0] = make_nv("method", "GET");
  nva[1] = make_nv("scheme", "https");
  nva[2] = make_nv("url", "/");
  nva[3] = make_nv("x-head", "foo");
  nva[4] = make_nv("x-head", "bar");
  nva[5] = make_nv("version", "HTTP/1.1");
  nva[6] = make_nv("x-empty", "");
  return nva;
}

static void check_frame_header(uint16_t length, uint8_t type, uint8_t flags,
                               int32_t stream_id, nghttp2_frame_hd *hd)
{
  CU_ASSERT(length == hd->length);
  CU_ASSERT(type == hd->type);
  CU_ASSERT(flags == hd->flags);
  CU_ASSERT(stream_id == hd->stream_id);
}

void test_nghttp2_frame_pack_headers()
{
  nghttp2_hd_deflater deflater;
  nghttp2_hd_inflater inflater;
  nghttp2_headers frame, oframe;
  nghttp2_bufs bufs;
  nghttp2_nv *nva;
  nghttp2_priority_spec pri_spec;
  ssize_t nvlen;
  nva_out out;
  ssize_t hdblocklen;
  int rv;

  frame_pack_bufs_init(&bufs);

  nva_out_init(&out);
  nghttp2_hd_deflate_init(&deflater);
  nghttp2_hd_inflate_init(&inflater);

  nva = headers();
  nvlen = HEADERS_LENGTH;

  nghttp2_priority_spec_default_init(&pri_spec);

  nghttp2_frame_headers_init(&frame,
                             NGHTTP2_FLAG_END_STREAM |
                             NGHTTP2_FLAG_END_HEADERS,
                             1000000007, NGHTTP2_HCAT_REQUEST,
                             &pri_spec, nva, nvlen);
  rv = nghttp2_frame_pack_headers(&bufs, &frame, &deflater);

  nghttp2_bufs_rewind(&bufs);

  CU_ASSERT(0 == rv);
  CU_ASSERT(nghttp2_bufs_len(&bufs) > 0);
  CU_ASSERT(0 == unpack_framebuf((nghttp2_frame*)&oframe, &bufs));

  check_frame_header(nghttp2_bufs_len(&bufs) - NGHTTP2_FRAME_HDLEN,
                     NGHTTP2_HEADERS,
                     NGHTTP2_FLAG_END_STREAM | NGHTTP2_FLAG_END_HEADERS,
                     1000000007, &oframe.hd);
  /* We did not include PRIORITY flag */
  CU_ASSERT(NGHTTP2_DEFAULT_WEIGHT == oframe.pri_spec.weight);

  hdblocklen = nghttp2_bufs_len(&bufs) - NGHTTP2_FRAME_HDLEN;
  CU_ASSERT(hdblocklen ==
            inflate_hd(&inflater, &out, &bufs, NGHTTP2_FRAME_HDLEN));

  CU_ASSERT(7 == out.nvlen);
  CU_ASSERT(nvnameeq("method", &out.nva[0]));
  CU_ASSERT(nvvalueeq("GET", &out.nva[0]));

  nghttp2_frame_headers_free(&oframe);
  nva_out_reset(&out);
  nghttp2_bufs_reset(&bufs);

  memset(&oframe, 0, sizeof(oframe));
  /* Next, include NGHTTP2_FLAG_PRIORITY */
  nghttp2_priority_spec_init(&frame.pri_spec, 1000000009, 12, 1);
  frame.hd.flags |= NGHTTP2_FLAG_PRIORITY;

  rv = nghttp2_frame_pack_headers(&bufs, &frame, &deflater);

  CU_ASSERT(0 == rv);
  CU_ASSERT(nghttp2_bufs_len(&bufs) > 0);
  CU_ASSERT(0 == unpack_framebuf((nghttp2_frame*)&oframe, &bufs));

  check_frame_header(nghttp2_bufs_len(&bufs) - NGHTTP2_FRAME_HDLEN,
                     NGHTTP2_HEADERS,
                     NGHTTP2_FLAG_END_STREAM | NGHTTP2_FLAG_END_HEADERS |
                     NGHTTP2_FLAG_PRIORITY,
                     1000000007, &oframe.hd);

  CU_ASSERT(1000000009 == oframe.pri_spec.stream_id);
  CU_ASSERT(12 == oframe.pri_spec.weight);
  CU_ASSERT(1 == oframe.pri_spec.exclusive);

  hdblocklen = nghttp2_bufs_len(&bufs) - NGHTTP2_FRAME_HDLEN
    - nghttp2_frame_priority_len(oframe.hd.flags);
  CU_ASSERT(hdblocklen ==
            inflate_hd(&inflater, &out, &bufs, NGHTTP2_FRAME_HDLEN
                       + nghttp2_frame_priority_len(oframe.hd.flags)));

  nghttp2_nv_array_sort(out.nva, out.nvlen);
  CU_ASSERT(nvnameeq("method", &out.nva[0]));

  nghttp2_frame_headers_free(&oframe);
  nva_out_reset(&out);
  nghttp2_bufs_reset(&bufs);

  nghttp2_bufs_free(&bufs);
  nghttp2_frame_headers_free(&frame);
  nghttp2_hd_inflate_free(&inflater);
  nghttp2_hd_deflate_free(&deflater);
}

void test_nghttp2_frame_pack_headers_frame_too_large(void)
{
  nghttp2_hd_deflater deflater;
  nghttp2_headers frame;
  nghttp2_bufs bufs;
  nghttp2_nv *nva;
  ssize_t nvlen;
  size_t big_vallen = NGHTTP2_HD_MAX_NV;
  nghttp2_nv big_hds[16];
  size_t big_hdslen = ARRLEN(big_hds);
  size_t i;
  int rv;

  frame_pack_bufs_init(&bufs);

  for(i = 0; i < big_hdslen; ++i) {
    big_hds[i].name = (uint8_t*)"header";
    big_hds[i].value = malloc(big_vallen+1);
    memset(big_hds[i].value, '0'+i, big_vallen);
    big_hds[i].value[big_vallen] = '\0';
    big_hds[i].namelen = strlen((char*)big_hds[i].name);
    big_hds[i].valuelen = big_vallen;
    big_hds[i].flags = NGHTTP2_NV_FLAG_NONE;
  }

  nvlen = nghttp2_nv_array_copy(&nva, big_hds, big_hdslen);
  nghttp2_hd_deflate_init(&deflater);
  nghttp2_frame_headers_init(&frame,
                             NGHTTP2_FLAG_END_STREAM|NGHTTP2_FLAG_END_HEADERS,
                             1000000007, NGHTTP2_HCAT_REQUEST,
                             NULL, nva, nvlen);
  rv = nghttp2_frame_pack_headers(&bufs, &frame, &deflater);
  CU_ASSERT(NGHTTP2_ERR_HEADER_COMP == rv);

  nghttp2_frame_headers_free(&frame);
  nghttp2_bufs_free(&bufs);
  for(i = 0; i < big_hdslen; ++i) {
    free(big_hds[i].value);
  }
  nghttp2_hd_deflate_free(&deflater);
}

void test_nghttp2_frame_pack_priority(void)
{
  nghttp2_priority frame, oframe;
  nghttp2_bufs bufs;
  nghttp2_priority_spec pri_spec;
  int rv;

  frame_pack_bufs_init(&bufs);

  /* First, pack priority with priority group and weight */
  nghttp2_priority_spec_init(&pri_spec, 1000000009, 12, 1);

  nghttp2_frame_priority_init(&frame, 1000000007, &pri_spec);
  rv = nghttp2_frame_pack_priority(&bufs, &frame);

  CU_ASSERT(0 == rv);
  CU_ASSERT(13 == nghttp2_bufs_len(&bufs));
  CU_ASSERT(0 == unpack_framebuf((nghttp2_frame*)&oframe, &bufs));
  check_frame_header(5, NGHTTP2_PRIORITY, NGHTTP2_FLAG_NONE,
                     1000000007, &oframe.hd);

  CU_ASSERT(1000000009 == oframe.pri_spec.stream_id);
  CU_ASSERT(12 == oframe.pri_spec.weight);
  CU_ASSERT(1 == oframe.pri_spec.exclusive);

  nghttp2_frame_priority_free(&oframe);
  nghttp2_bufs_reset(&bufs);

  nghttp2_bufs_free(&bufs);
  nghttp2_frame_priority_free(&frame);
}

void test_nghttp2_frame_pack_rst_stream(void)
{
  nghttp2_rst_stream frame, oframe;
  nghttp2_bufs bufs;
  int rv;

  frame_pack_bufs_init(&bufs);

  nghttp2_frame_rst_stream_init(&frame, 1000000007, NGHTTP2_PROTOCOL_ERROR);
  rv = nghttp2_frame_pack_rst_stream(&bufs, &frame);

  CU_ASSERT(0 == rv);
  CU_ASSERT(12 == nghttp2_bufs_len(&bufs));
  CU_ASSERT(0 == unpack_framebuf((nghttp2_frame*)&oframe, &bufs));
  check_frame_header(4, NGHTTP2_RST_STREAM, NGHTTP2_FLAG_NONE, 1000000007,
                     &oframe.hd);
  CU_ASSERT(NGHTTP2_PROTOCOL_ERROR == oframe.error_code);

  nghttp2_bufs_free(&bufs);
  nghttp2_frame_rst_stream_free(&oframe);
  nghttp2_frame_rst_stream_free(&frame);
}

void test_nghttp2_frame_pack_settings()
{
  nghttp2_settings frame, oframe;
  nghttp2_bufs bufs;
  int i;
  int rv;
  nghttp2_settings_entry iv[] =
    {
      {
        NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 256
      },
      {
        NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 16384
      },
      {
        NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, 4096
      }
    };

  frame_pack_bufs_init(&bufs);

  nghttp2_frame_settings_init(&frame, NGHTTP2_FLAG_NONE,
                              nghttp2_frame_iv_copy(iv, 3), 3);
  rv = nghttp2_frame_pack_settings(&bufs, &frame);

  CU_ASSERT(0 == rv);
  CU_ASSERT(NGHTTP2_FRAME_HDLEN + 3 * NGHTTP2_FRAME_SETTINGS_ENTRY_LENGTH ==
            nghttp2_bufs_len(&bufs));

  CU_ASSERT(0 == unpack_framebuf((nghttp2_frame*)&oframe, &bufs));
  check_frame_header(3 * NGHTTP2_FRAME_SETTINGS_ENTRY_LENGTH,
                     NGHTTP2_SETTINGS, NGHTTP2_FLAG_NONE, 0, &oframe.hd);
  CU_ASSERT(3 == oframe.niv);
  for(i = 0; i < 3; ++i) {
    CU_ASSERT(iv[i].settings_id == oframe.iv[i].settings_id);
    CU_ASSERT(iv[i].value == oframe.iv[i].value);
  }

  nghttp2_bufs_free(&bufs);
  nghttp2_frame_settings_free(&frame);
  nghttp2_frame_settings_free(&oframe);
}

void test_nghttp2_frame_pack_push_promise()
{
  nghttp2_hd_deflater deflater;
  nghttp2_hd_inflater inflater;
  nghttp2_push_promise frame, oframe;
  nghttp2_bufs bufs;
  nghttp2_nv *nva;
  ssize_t nvlen;
  nva_out out;
  ssize_t hdblocklen;
  int rv;

  frame_pack_bufs_init(&bufs);

  nva_out_init(&out);
  nghttp2_hd_deflate_init(&deflater);
  nghttp2_hd_inflate_init(&inflater);

  nva = headers();
  nvlen = HEADERS_LENGTH;
  nghttp2_frame_push_promise_init(&frame, NGHTTP2_FLAG_END_HEADERS,
                                  1000000007, (1U << 31) - 1, nva, nvlen);
  rv = nghttp2_frame_pack_push_promise(&bufs, &frame, &deflater);

  CU_ASSERT(0 == rv);
  CU_ASSERT(nghttp2_bufs_len(&bufs) > 0);
  CU_ASSERT(0 == unpack_framebuf((nghttp2_frame*)&oframe, &bufs));

  check_frame_header(nghttp2_bufs_len(&bufs) - NGHTTP2_FRAME_HDLEN,
                     NGHTTP2_PUSH_PROMISE,
                     NGHTTP2_FLAG_END_HEADERS, 1000000007, &oframe.hd);
  CU_ASSERT((1U << 31) - 1 == oframe.promised_stream_id);

  hdblocklen = nghttp2_bufs_len(&bufs) - NGHTTP2_FRAME_HDLEN - 4;
  CU_ASSERT(hdblocklen ==
            inflate_hd(&inflater, &out, &bufs, NGHTTP2_FRAME_HDLEN + 4));

  CU_ASSERT(7 == out.nvlen);
  CU_ASSERT(nvnameeq("method", &out.nva[0]));
  CU_ASSERT(nvvalueeq("GET", &out.nva[0]));

  nva_out_reset(&out);
  nghttp2_bufs_free(&bufs);
  nghttp2_frame_push_promise_free(&oframe);
  nghttp2_frame_push_promise_free(&frame);
  nghttp2_hd_inflate_free(&inflater);
  nghttp2_hd_deflate_free(&deflater);
}

void test_nghttp2_frame_pack_ping(void)
{
  nghttp2_ping frame, oframe;
  nghttp2_bufs bufs;
  const uint8_t opaque_data[] = "01234567";
  int rv;

  frame_pack_bufs_init(&bufs);

  nghttp2_frame_ping_init(&frame, NGHTTP2_FLAG_ACK, opaque_data);
  rv = nghttp2_frame_pack_ping(&bufs, &frame);

  CU_ASSERT(0 == rv);
  CU_ASSERT(16 == nghttp2_bufs_len(&bufs));
  CU_ASSERT(0 == unpack_framebuf((nghttp2_frame*)&oframe, &bufs));
  check_frame_header(8, NGHTTP2_PING, NGHTTP2_FLAG_ACK, 0, &oframe.hd);
  CU_ASSERT(memcmp(opaque_data, oframe.opaque_data, sizeof(opaque_data) - 1)
            == 0);

  nghttp2_bufs_free(&bufs);
  nghttp2_frame_ping_free(&oframe);
  nghttp2_frame_ping_free(&frame);
}

void test_nghttp2_frame_pack_goaway()
{
  nghttp2_goaway frame, oframe;
  nghttp2_bufs bufs;
  size_t opaque_data_len = 16;
  uint8_t *opaque_data = malloc(opaque_data_len);
  int rv;

  frame_pack_bufs_init(&bufs);

  memcpy(opaque_data, "0123456789abcdef", opaque_data_len);
  nghttp2_frame_goaway_init(&frame, 1000000007, NGHTTP2_PROTOCOL_ERROR,
                            opaque_data, opaque_data_len);
  rv = nghttp2_frame_pack_goaway(&bufs, &frame);

  CU_ASSERT(0 == rv);
  CU_ASSERT((ssize_t)(16 + opaque_data_len) == nghttp2_bufs_len(&bufs));
  CU_ASSERT(0 == unpack_framebuf((nghttp2_frame*)&oframe, &bufs));
  check_frame_header(24, NGHTTP2_GOAWAY, NGHTTP2_FLAG_NONE, 0, &oframe.hd);
  CU_ASSERT(1000000007 == oframe.last_stream_id);
  CU_ASSERT(NGHTTP2_PROTOCOL_ERROR == oframe.error_code);

  CU_ASSERT(opaque_data_len == oframe.opaque_data_len);
  CU_ASSERT(memcmp(opaque_data, oframe.opaque_data, opaque_data_len) == 0);

  nghttp2_bufs_free(&bufs);
  nghttp2_frame_goaway_free(&oframe);
  nghttp2_frame_goaway_free(&frame);
}

void test_nghttp2_frame_pack_window_update(void)
{
  nghttp2_window_update frame, oframe;
  nghttp2_bufs bufs;
  int rv;

  frame_pack_bufs_init(&bufs);

  nghttp2_frame_window_update_init(&frame, NGHTTP2_FLAG_NONE,
                                   1000000007, 4096);
  rv = nghttp2_frame_pack_window_update(&bufs, &frame);

  CU_ASSERT(0 == rv);
  CU_ASSERT(12 == nghttp2_bufs_len(&bufs));
  CU_ASSERT(0 == unpack_framebuf((nghttp2_frame*)&oframe, &bufs));
  check_frame_header(4, NGHTTP2_WINDOW_UPDATE, NGHTTP2_FLAG_NONE,
                     1000000007, &oframe.hd);
  CU_ASSERT(4096 == oframe.window_size_increment);

  nghttp2_bufs_free(&bufs);
  nghttp2_frame_window_update_free(&oframe);
  nghttp2_frame_window_update_free(&frame);
}

void test_nghttp2_frame_pack_altsvc(void)
{
  nghttp2_altsvc frame, oframe;
  nghttp2_bufs bufs;
  nghttp2_buf *buf;
  size_t protocol_id_len, host_len, origin_len;
  uint8_t *protocol_id, *host, *origin;
  uint8_t *data;
  size_t datalen;
  int rv;
  size_t payloadlen;

  protocol_id_len = strlen("h2");
  host_len = strlen("h2.example.org");
  origin_len = strlen("www.example.org");

  datalen = protocol_id_len + host_len + origin_len;
  data = malloc(datalen);

  memcpy(data, "h2", protocol_id_len);
  protocol_id = data;

  memcpy(data + protocol_id_len, "h2.example.org", host_len);
  host = data + protocol_id_len;

  memcpy(data + protocol_id_len + host_len,
         "http://www.example.org", origin_len);
  origin = data + protocol_id_len + host_len;

  frame_pack_bufs_init(&bufs);

  nghttp2_frame_altsvc_init(&frame, 1000000007, 1u << 31, 4000,
                            protocol_id, protocol_id_len,
                            host, host_len, origin, origin_len);

  rv = nghttp2_frame_pack_altsvc(&bufs, &frame);

  CU_ASSERT(0 == rv);

  CU_ASSERT((ssize_t)(NGHTTP2_FRAME_HDLEN + NGHTTP2_ALTSVC_MINLEN + datalen) ==
            nghttp2_bufs_len(&bufs));

  CU_ASSERT(0 == unpack_framebuf((nghttp2_frame*)&oframe, &bufs));

  check_frame_header(NGHTTP2_ALTSVC_MINLEN + datalen,
                     NGHTTP2_ALTSVC, NGHTTP2_FLAG_NONE,
                     1000000007, &oframe.hd);
  CU_ASSERT(1u << 31 == oframe.max_age);
  CU_ASSERT(4000 == oframe.port);

  CU_ASSERT(protocol_id_len == oframe.protocol_id_len);
  CU_ASSERT(memcmp(protocol_id, oframe.protocol_id, protocol_id_len) == 0);

  CU_ASSERT(host_len == oframe.host_len);
  CU_ASSERT(memcmp(host, oframe.host, host_len) == 0);

  CU_ASSERT(origin_len == oframe.origin_len);
  CU_ASSERT(memcmp(origin, oframe.origin, origin_len) == 0);

  nghttp2_frame_altsvc_free(&oframe);
  nghttp2_frame_altsvc_free(&frame);

  memset(&oframe, 0, sizeof(oframe));

  buf = &bufs.head->buf;

  CU_ASSERT(buf->pos - buf->begin == 1);

  /* Check no origin case */

  payloadlen = NGHTTP2_ALTSVC_MINLEN + protocol_id_len + host_len;
  nghttp2_put_uint16be(buf->pos, payloadlen);

  CU_ASSERT(0 ==
            nghttp2_frame_unpack_altsvc_payload
            (&oframe,
             buf->pos + NGHTTP2_FRAME_HDLEN,
             NGHTTP2_ALTSVC_FIXED_PARTLEN,
             buf->pos + NGHTTP2_FRAME_HDLEN + NGHTTP2_ALTSVC_FIXED_PARTLEN,
             payloadlen - NGHTTP2_ALTSVC_FIXED_PARTLEN));

  CU_ASSERT(host_len == oframe.host_len);
  CU_ASSERT(0 == oframe.origin_len);

  /* Check insufficient payload length for host */
  payloadlen = NGHTTP2_ALTSVC_MINLEN + protocol_id_len + host_len - 1;
  nghttp2_put_uint16be(buf->pos, payloadlen);

  CU_ASSERT(NGHTTP2_ERR_FRAME_SIZE_ERROR ==
            nghttp2_frame_unpack_altsvc_payload
            (&oframe,
             buf->pos + NGHTTP2_FRAME_HDLEN,
             NGHTTP2_ALTSVC_FIXED_PARTLEN,
             buf->pos + NGHTTP2_FRAME_HDLEN + NGHTTP2_ALTSVC_FIXED_PARTLEN,
             payloadlen - NGHTTP2_ALTSVC_FIXED_PARTLEN));

  /* Check no host case */
  payloadlen = NGHTTP2_ALTSVC_MINLEN + protocol_id_len;
  nghttp2_put_uint16be(buf->pos, payloadlen);
  buf->pos[NGHTTP2_FRAME_HDLEN + NGHTTP2_ALTSVC_FIXED_PARTLEN
           + protocol_id_len] = 0;

  CU_ASSERT(0 ==
            nghttp2_frame_unpack_altsvc_payload
            (&oframe,
             buf->pos + NGHTTP2_FRAME_HDLEN,
             NGHTTP2_ALTSVC_FIXED_PARTLEN,
             buf->pos + NGHTTP2_FRAME_HDLEN + NGHTTP2_ALTSVC_FIXED_PARTLEN,
             payloadlen - NGHTTP2_ALTSVC_FIXED_PARTLEN));

  CU_ASSERT(0 == oframe.host_len);
  CU_ASSERT(0 == oframe.origin_len);

  /* Check missing Host-Len */
  payloadlen = NGHTTP2_ALTSVC_FIXED_PARTLEN + protocol_id_len;
  nghttp2_put_uint16be(buf->pos, payloadlen);

  CU_ASSERT(NGHTTP2_ERR_FRAME_SIZE_ERROR ==
            nghttp2_frame_unpack_altsvc_payload
            (&oframe,
             buf->pos + NGHTTP2_FRAME_HDLEN,
             NGHTTP2_ALTSVC_FIXED_PARTLEN,
             buf->pos + NGHTTP2_FRAME_HDLEN + NGHTTP2_ALTSVC_FIXED_PARTLEN,
             payloadlen - NGHTTP2_ALTSVC_FIXED_PARTLEN));

  nghttp2_bufs_free(&bufs);
}

void test_nghttp2_nv_array_copy(void)
{
  nghttp2_nv *nva;
  ssize_t rv;
  nghttp2_nv emptynv[] = {MAKE_NV("", ""),
                          MAKE_NV("", "")};
  nghttp2_nv nv[] = {MAKE_NV("alpha", "bravo"),
                     MAKE_NV("charlie", "delta")};
  nghttp2_nv bignv;

  bignv.name = (uint8_t*)"echo";
  bignv.namelen = strlen("echo");
  bignv.valuelen = (1 << 14) - 1;
  bignv.value = malloc(bignv.valuelen);
  memset(bignv.value, '0', bignv.valuelen);

  rv = nghttp2_nv_array_copy(&nva, NULL, 0);
  CU_ASSERT(0 == rv);
  CU_ASSERT(NULL == nva);

  rv = nghttp2_nv_array_copy(&nva, emptynv, ARRLEN(emptynv));
  CU_ASSERT(0 == rv);
  CU_ASSERT(NULL == nva);

  rv = nghttp2_nv_array_copy(&nva, nv, ARRLEN(nv));
  CU_ASSERT(2 == rv);
  CU_ASSERT(nva[0].namelen == 5);
  CU_ASSERT(0 == memcmp("alpha", nva[0].name, 5));
  CU_ASSERT(nva[0].valuelen = 5);
  CU_ASSERT(0 == memcmp("bravo", nva[0].value, 5));
  CU_ASSERT(nva[1].namelen == 7);
  CU_ASSERT(0 == memcmp("charlie", nva[1].name, 7));
  CU_ASSERT(nva[1].valuelen == 5);
  CU_ASSERT(0 == memcmp("delta", nva[1].value, 5));

  nghttp2_nv_array_del(nva);

  /* Large header field is acceptable */
  rv = nghttp2_nv_array_copy(&nva, &bignv, 1);
  CU_ASSERT(1 == rv);

  nghttp2_nv_array_del(nva);

  free(bignv.value);
}

void test_nghttp2_iv_check(void)
{
  nghttp2_settings_entry iv[5];

  iv[0].settings_id = NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS;
  iv[0].value = 100;
  iv[1].settings_id = NGHTTP2_SETTINGS_HEADER_TABLE_SIZE;
  iv[1].value = 1024;

  CU_ASSERT(nghttp2_iv_check(iv, 2));

  iv[1].settings_id = NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE;
  iv[1].value = NGHTTP2_MAX_WINDOW_SIZE;
  CU_ASSERT(nghttp2_iv_check(iv, 2));

  /* Too large window size */
  iv[1].value = (uint32_t)NGHTTP2_MAX_WINDOW_SIZE + 1;
  CU_ASSERT(0 == nghttp2_iv_check(iv, 2));

  /* ENABLE_PUSH only allows 0 or 1 */
  iv[1].settings_id = NGHTTP2_SETTINGS_ENABLE_PUSH;
  iv[1].value = 0;
  CU_ASSERT(nghttp2_iv_check(iv, 2));
  iv[1].value = 1;
  CU_ASSERT(nghttp2_iv_check(iv, 2));
  iv[1].value = 3;
  CU_ASSERT(!nghttp2_iv_check(iv, 2));

  /* Undefined SETTINGS ID */
  iv[1].settings_id = 1000000009;
  iv[1].value = 0;
  CU_ASSERT(!nghttp2_iv_check(iv, 2));

  /* Too large SETTINGS_HEADER_TABLE_SIZE */
  iv[1].settings_id = NGHTTP2_SETTINGS_HEADER_TABLE_SIZE;
  iv[1].value = UINT32_MAX;
  CU_ASSERT(!nghttp2_iv_check(iv, 2));
}
