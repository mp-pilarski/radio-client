# Internet Radio Client

`radio` is a command-line Internet radio client. It connects to a stream over TCP/IPv4 or TCP/IPv6, writes received audio bytes unchanged to standard output, and can optionally extract ICY text metadata. You need an audio player to listen to the stream.

The client supports `http://` and `https://` stream URLs. HTTPS uses OpenSSL and the system's default trusted certificate store.
## Requirements

- Linux or another POSIX environment providing sockets, `poll`, `getaddrinfo`, and `getopt`;
- a C++20 compiler (the Makefile uses `g++`);
- OpenSSL development libraries and headers (`libssl` and `libcrypto`);
- an audio player capable of reading the stream from standard input, such as `play` or `mpv`.

## Build

```sh
make
```

## Usage

```text
sikradio -u URL [-m] [-t TIMEOUT] [-4 | -6] [-v VERBOSITY] [-q]
```

`-u URL` is required. Options may be written in any order and short flags may be combined (for example, `-m46`).

| Option | Meaning | Default |
| --- | --- | --- |
| `-u URL` | Stream URL, such as `http://radio.example/stream` or `https://radio.example/stream` | required |
| `-m` | Request ICY metadata multiplexed with audio; extracted metadata goes to standard error | off |
| `-t MILLISECONDS` | Maximum interval without received stream data before reconnecting; valid range `100`–`100000` | `5000` |
| `-4` | Force IPv4 | automatic |
| `-6` | Force IPv6 | automatic |
| `-v LEVEL` | Diagnostic verbosity from `0` to `4` | `2` |
| `-q` | Quiet mode, equivalent to `-v0` | — |

If neither address-family flag is supplied, the client uses the first address family returned by `getaddrinfo`. Supplying both `-4` and `-6` has the same effect as supplying neither.

## Playing the audio

Keep standard output connected to the player and standard error available for diagnostics:

```sh
./radio -u 'https://rs101-krk.rmfstream.pl/rmf_fm' -m | play -q -t mp3 -
```

With `mpv`:

```sh
./radio -u 'https://rs101-krk.rmfstream.pl/rmf_fm' | mpv --really-quiet -
```

Metadata can be redirected separately:

```sh
./radio -m -u 'https://rs101-krk.rmfstream.pl/rmf_fm' 2>metadata.log | mpv --really-quiet -
```

## Diagnostics

Errors are written to standard error. Additional output is enabled up to the selected verbosity level:

| Level | Additional output |
| ---: | --- |
| `0` | No additional diagnostic output |
| `1` | Communication progress and reconnect information |
| `2` | Critical errors preventing continued operation |
| `3` | Non-critical system or library errors |
| `4` | Detailed debugging information |


## Stopping and exit status

Type `quit` followed by Enter on standard input to stop cleanly. The client also stops cleanly when the server closes the connection. Data received so far is written before termination.

- `0` — clean termination by `quit` or server close;
- `1` — invalid arguments or a critical error that prevents continuing.

## Example radio stations
- http://stream3.polskieradio.pl:8900
- http://an04.cdn.eurozet.pl/ant-web.mp3
- https://stream.nowyswiat.online/mp3
- https://rs101-krk.rmfstream.pl/rmf_fm