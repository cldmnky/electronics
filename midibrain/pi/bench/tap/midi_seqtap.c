// midi_seqtap - read-only ALSA sequencer tap for the MIDI Brain bench.
//
// Subscribes (READ only) to every exported source port and logs each MIDI
// event as a CSV row:
//
//   mono_us,alsa_time_raw,time_stamp_kind,src_client,src_port,type,hex
//
// `mono_us` is CLOCK_MONOTONIC taken immediately after the event was read back
// (common time base for every source, so send/receive deltas are meaningful);
// `alsa_time_raw` is the sequencer's own timestamp field, kept uninterpreted
// because RtMidi sources may stamp tick or real time.
//
// The tap never writes to a port, never changes a queue and never opens a
// rawmidi or PCM device: running it cannot disturb the live rig, and its
// subscriptions disappear when the process exits. This is what makes it safe
// against the owner's running app while still giving real wire timestamps.
//
// usage: midi_seqtap <seconds> [out.csv]
//   topology (clients, ports, caps) goes to stderr.
#define _POSIX_C_SOURCE 200809L
#include <alsa/asoundlib.h>
#include <alloca.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t stop = 0;
static void on_signal(int sig) { (void)sig; stop = 1; }

static unsigned long long mono_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long long)ts.tv_sec * 1000000ULL + (unsigned long long)ts.tv_nsec / 1000ULL;
}

static void print_caps(snd_seq_port_info_t *p) {
  unsigned int caps = snd_seq_port_info_get_capability(p);
  char buf[7];
  int i = 0;
  buf[i++] = (caps & SND_SEQ_PORT_CAP_READ) ? 'r' : '-';
  buf[i++] = (caps & SND_SEQ_PORT_CAP_WRITE) ? 'w' : '-';
  buf[i++] = (caps & SND_SEQ_PORT_CAP_SUBS_READ) ? 'R' : '-';
  buf[i++] = (caps & SND_SEQ_PORT_CAP_SUBS_WRITE) ? 'W' : '-';
  buf[i++] = (caps & SND_SEQ_PORT_CAP_NO_EXPORT) ? 'x' : '-';
  buf[i] = '\0';
  fprintf(stderr, "  %3d:%-3d %-24s caps=%s type=0x%08x\n",
          snd_seq_port_info_get_client(p), snd_seq_port_info_get_port(p),
          snd_seq_port_info_get_name(p), buf, snd_seq_port_info_get_type(p));
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <seconds> [out.csv]\n", argv[0]);
    return 2;
  }
  const double seconds = atof(argv[1]);
  FILE *out = (argc > 2) ? fopen(argv[2], "w") : stdout;
  if (!out) {
    perror("fopen");
    return 1;
  }
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  snd_seq_t *seq = NULL;
  if (snd_seq_open(&seq, "hw", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) < 0) {
    fprintf(stderr, "snd_seq_open(hw) failed\n");
    return 1;
  }
  snd_seq_set_client_name(seq, "bench-tap");
  const int my_client = snd_seq_client_id(seq);
  const int my_port =
      snd_seq_create_simple_port(seq, "bench-tap",
                                 SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                 SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
  if (my_port < 0) {
    fprintf(stderr, "create port failed: %s\n", snd_strerror(my_port));
    return 1;
  }

  // Topology + read-only subscriptions to every exported source port.
  fprintf(stderr, "tap: client %d port %d, %llu\n", my_client, my_port, mono_us());
  int subscribed = 0;
  snd_seq_client_info_t *cinfo;
  snd_seq_port_info_t *pinfo;
  snd_seq_client_info_alloca(&cinfo);
  snd_seq_port_info_alloca(&pinfo);
  snd_seq_client_info_set_client(cinfo, -1);
  while (snd_seq_query_next_client(seq, cinfo) >= 0) {
    const int client = snd_seq_client_info_get_client(cinfo);
    if (client == my_client) continue;
    fprintf(stderr, "client %3d: %s\n", client, snd_seq_client_info_get_name(cinfo));
    snd_seq_port_info_set_client(pinfo, client);
    snd_seq_port_info_set_port(pinfo, -1);
    while (snd_seq_query_next_port(seq, pinfo) >= 0) {
      print_caps(pinfo);
      const unsigned int caps = snd_seq_port_info_get_capability(pinfo);
      if (!(caps & SND_SEQ_PORT_CAP_READ)) continue;   // not a source
      if (!(caps & SND_SEQ_PORT_CAP_SUBS_READ)) continue;  // cannot be subscribed
      if (caps & SND_SEQ_PORT_CAP_NO_EXPORT) continue;
      snd_seq_port_subscribe_t *sub;
      snd_seq_port_subscribe_alloca(&sub);
      snd_seq_addr_t sender;
      snd_seq_addr_t dest;
      sender.client = client;
      sender.port = snd_seq_port_info_get_port(pinfo);
      dest.client = my_client;
      dest.port = my_port;
      snd_seq_port_subscribe_set_sender(sub, &sender);
      snd_seq_port_subscribe_set_dest(sub, &dest);
      snd_seq_port_subscribe_set_time_update(sub, 1);
      const int rc = snd_seq_subscribe_port(seq, sub);
      fprintf(stderr, "  subscribe %d:%d -> %d:%d : %s\n", sender.client, sender.port,
              dest.client, dest.port, rc < 0 ? snd_strerror(rc) : "ok");
      if (rc >= 0) ++subscribed;
    }
  }
  fprintf(stderr, "tap: %d subscription(s)\n", subscribed);

  snd_midi_event_t *decoder;
  if (snd_midi_event_new(256, &decoder) < 0) return 1;
  snd_midi_event_no_status(decoder, 1);  // decode to full messages, not running status

  fputs("mono_us,alsa_time_raw,time_kind,src,type,hex\n", out);
  fflush(out);

  const unsigned long long deadline = mono_us() + (unsigned long long)(seconds * 1e6);
  unsigned long long rows = 0;
  unsigned long long per_client[256] = {0};
  unsigned char bytes[256];
  snd_seq_event_t *ev = NULL;

  while (!stop && mono_us() < deadline) {
    const int rc = snd_seq_event_input(seq, &ev);
    if (rc == -EAGAIN) {
      struct pollfd local[8];
      const int nfds = snd_seq_poll_descriptors(seq, local, 8, POLLIN);
      poll(local, nfds > 0 ? nfds : 0, 100);
      continue;
    }
    if (rc < 0) {
      fprintf(stderr, "event_input: %s\n", snd_strerror(rc));
      continue;
    }
    if (!ev) continue;
    const unsigned long long now = mono_us();
    const int src_client = ev->source.client;
    const int src_port = ev->source.port;
    if (src_client >= 0 && src_client < 256) ++per_client[src_client];

    const long count = snd_midi_event_decode(decoder, bytes, sizeof(bytes), ev);
    char hex[3 * 32 + 1];
    hex[0] = '\0';
    if (count > 0) {
      const long n = count > 32 ? 32 : count;
      for (long i = 0; i < n; ++i) snprintf(hex + 3 * i, 4, "%02x ", bytes[i]);
      hex[3 * n - 1] = '\0';
    }
    const unsigned int raw_time = ev->time.tick;
    const char *kind = (ev->flags & SND_SEQ_TIME_STAMP_MASK) == SND_SEQ_TIME_STAMP_REAL ? "real" : "tick";
    fprintf(out, "%llu,%u,%s,%d:%d,%d,%s\n", now, raw_time, kind, src_client, src_port,
            ev->type, hex);
    ++rows;
    if ((rows & 0x3f) == 0) fflush(out);
  }

  fprintf(stderr, "tap: %llu rows\n", rows);
  for (int c = 0; c < 256; ++c) {
    if (per_client[c]) fprintf(stderr, "tap: client %d events %llu\n", c, per_client[c]);
  }
  snd_midi_event_free(decoder);
  snd_seq_close(seq);
  if (out != stdout) fclose(out);
  return 0;
}
