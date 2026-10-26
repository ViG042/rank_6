#!/usr/bin/env python3
"""Tests simules de mini_serv avec select : readiness, allocations et fermetures.

Usage : python3 tests/test_mini_serv_select.py [chemin/solution.c] [--valgrind]
Le simulateur ne remplace pas les essais TCP ni le correcteur de l'examen.
"""
import pathlib
import subprocess
import sys
import tempfile

HARNESS = r'''
#include <sys/socket.h>
#include <sys/select.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int student_main(int, char **);
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);

struct event { int kind; int fd; const char *text; };
/* kind : 1 = connexion, 2 = réception, 3 = déconnexion */
EVENTS
EXPECTED
static size_t event_index, read_offset;
static int idle_count, live[32], allocation_calls, calloc_calls;
static char received[32][65536];
static size_t received_len[32];

static void fail(const char *reason)
{
    fprintf(stderr, "SIMULATION: %s\n", reason);
    exit(3);
}

void *__wrap_malloc(size_t size)
{
    if (++allocation_calls == FAIL_ALLOC)
        return NULL;
    return __real_malloc(size);
}

void *__wrap_realloc(void *ptr, size_t size)
{
    if (++allocation_calls == FAIL_ALLOC)
        return NULL;
    return __real_realloc(ptr, size);
}

void *__wrap_calloc(size_t n, size_t size)
{
    if (++calloc_calls == FAIL_CALLOC)
        return NULL;
    return __real_calloc(n, size);
}

int __wrap_socket(int domain, int type, int protocol)
{
    if (domain != AF_INET || type != SOCK_STREAM || protocol != 0)
        fail("socket TCP IPv4 attendue");
    live[3] = 1;
    return 3;
}

int __wrap_bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    (void)fd; (void)addr; (void)len;
    return FAIL_BIND ? -1 : 0;
}

int __wrap_listen(int fd, int backlog)
{
    (void)fd; (void)backlog;
    return 0;
}

static void verify(void)
{
    if (EXPECT_FATAL)
        fail("Fatal error et exit(1) étaient attendus");
    for (int fd = 4; fd < 32; ++fd)
    {
        const char *want = expected[fd] ? expected[fd] : "";
        if (strcmp(received[fd], want))
        {
            fprintf(stderr, "SIMULATION: sortie incorrecte sur fd %d "
                "(attendu %zu octets, reçu %zu)\n", fd,
                strlen(want), received_len[fd]);
            exit(3);
        }
    }
    exit(0);
}

int __wrap_select(int n, fd_set *r, fd_set *w, fd_set *e,
                  struct timeval *timeout)
{
    (void)e; (void)timeout;
    if (!r)
        fail("ce simulateur attend un ensemble de lecture");
    if (event_index < sizeof(events) / sizeof(events[0]))
    {
        int next = events[event_index].kind == 1 ? 3 : events[event_index].fd;
        if (next >= n || !FD_ISSET(next, r))
            fail("select ne surveille pas le descripteur attendu");
    }
    FD_ZERO(r);
    if (w)
    {
        FD_CLR(3, w);
        for (int fd = 4; fd < n && fd < 32; ++fd)
            if (!live[fd])
                FD_CLR(fd, w);
    }
    if (event_index == sizeof(events) / sizeof(events[0]))
    {
        if (++idle_count == 16)
            verify();
        return 1; /* laisse au programme le temps de vider ses sorties */
    }
    const struct event *ev = &events[event_index];
    int fd = ev->kind == 1 ? 3 : ev->fd;
    if (fd >= n)
        fail("select ne surveille pas le descripteur attendu");
    FD_SET(fd, r);
    if (w && ev->kind == 2 && SKIP_WRITABLE)
        FD_CLR(SKIP_WRITABLE, w);
    return 1;
}

int __wrap_accept(int fd, struct sockaddr *addr, socklen_t *len)
{
    (void)addr; (void)len;
    if (fd != 3 || event_index >= sizeof(events) / sizeof(events[0])
        || events[event_index].kind != 1)
        fail("accept appelé sans connexion prête");
    int client = events[event_index++].fd;
    live[client] = 1;
    return client;
}

ssize_t __wrap_recv(int fd, void *buf, size_t size, int flags)
{
    (void)flags;
    if (event_index >= sizeof(events) / sizeof(events[0]) || !size)
        fail("recv inattendu");
    const struct event *ev = &events[event_index];
    if (ev->fd != fd || ev->kind == 1)
        fail("recv appelé sans données prêtes");
    if (ev->kind == 3)
    {
        ++event_index;
        return 0;
    }
    size_t remaining = strlen(ev->text) - read_offset;
    size_t count = remaining < size ? remaining : size;
    memcpy(buf, ev->text + read_offset, count);
    read_offset += count;
    if (read_offset == strlen(ev->text))
    {
        read_offset = 0;
        ++event_index;
    }
    return (ssize_t)count;
}

ssize_t __wrap_send(int fd, const void *buf, size_t size, int flags)
{
    (void)flags;
    if (fd < 4 || fd >= 32 || !live[fd])
        fail("send sur un descripteur qui n'est pas un client actif");
    if (SHORT_SEND && size > 1)
        size /= 2;
    if (received_len[fd] + size >= sizeof(received[fd]))
        fail("sortie trop grande pour le simulateur");
    memcpy(received[fd] + received_len[fd], buf, size);
    received_len[fd] += size;
    received[fd][received_len[fd]] = 0;
    return (ssize_t)size;
}

int __wrap_close(int fd)
{
    if (fd < 3 || fd >= 32 || !live[fd])
        fail("close sur un descripteur invalide");
    live[fd] = 0;
    return 0;
}

int main(int argc, char **argv)
{
    return student_main(argc, argv);
}
'''


def c_string(value):
    # Chaque octet est échappé en octal : pas d'interpolation de code C.
    return '"' + ''.join(f'\\{b:03o}' for b in value.encode()) + '"'


CONNECT = [(1, 4, ''), (1, 5, '')]
ARRIVED = 'server: client 1 just arrived\n'
LONG = 'x' * 5000


def scenario(name, events=CONNECT, expected=None, args=('4242',),
             returncode=0, stderr=b'', **options):
    return dict(name=name, events=events, expected=expected or {}, args=args,
                returncode=returncode, stderr=stderr, options=options)


CASES = [
    scenario('Sans argument', events=[], args=(), returncode=1,
             stderr=b'Wrong number of arguments\n'),
    scenario('Trop d’arguments', events=[], args=('4242', 'extra'), returncode=1,
             stderr=b'Wrong number of arguments\n'),
    scenario('Échec de bind', events=[], returncode=1,
             stderr=b'Fatal error\n', FAIL_BIND=1),
    scenario('Arrivée de deux clients', expected={4: ARRIVED}),
    scenario('Diffusion sans écho à l’auteur',
             events=CONNECT + [(2, 4, 'bonjour\n')],
             expected={4: ARRIVED, 5: 'client 0: bonjour\n'}),
    scenario('Ligne fragmentée et suffixe incomplet',
             events=CONNECT + [(2, 4, 'bon'), (2, 4, 'jour\nsui'),
                               (2, 4, 'te\nfin')],
             expected={4: ARRIVED, 5: 'client 0: bonjour\nclient 0: suite\n'}),
    scenario('Plusieurs lignes, dont une vide',
             events=CONNECT + [(2, 4, 'a\n\nb\n')],
             expected={4: ARRIVED, 5: 'client 0: a\nclient 0: \nclient 0: b\n'}),
    scenario('Ligne de 5000 caractères',
             events=CONNECT + [(2, 4, LONG + '\n')],
             expected={4: ARRIVED, 5: 'client 0: ' + LONG + '\n'}),
    scenario('Buffers indépendants pour deux auteurs',
             events=CONNECT + [(2, 4, 'pre'), (2, 5, 'autre\n'),
                               (2, 4, 'mier\n')],
             expected={4: ARRIVED + 'client 1: autre\n',
                       5: 'client 0: premier\n'}),
    scenario('Départ puis réutilisation du fd avec un nouvel id',
             events=CONNECT + [(2, 5, 'incomplet'), (3, 5, ''),
                               (1, 5, ''), (2, 5, 'nouveau\n')],
             expected={4: ARRIVED + 'server: client 1 just left\n'
                       'server: client 2 just arrived\nclient 2: nouveau\n'}),
    scenario('Échec du premier malloc/realloc [injection]',
             events=CONNECT + [(2, 4, 'bonjour\n')], returncode=1,
             stderr=b'Fatal error\n', FAIL_ALLOC=1, EXPECT_FATAL=1),
    scenario('Échec d’allocation avec une entrée existante [injection]',
             events=CONNECT + [(2, 4, 'bon'), (2, 4, 'jour\n')], returncode=1,
             stderr=b'Fatal error\n', FAIL_ALLOC=3, EXPECT_FATAL=1),
    scenario('Destinataire temporairement non prêt [simulation]',
             events=CONNECT + [(2, 4, 'bonjour\n')],
             expected={4: ARRIVED, 5: 'client 0: bonjour\n'}, SKIP_WRITABLE=5),
    scenario('Envois partiels [simulation]',
             events=CONNECT + [(2, 4, 'bonjour\n')],
             expected={4: ARRIVED, 5: 'client 0: bonjour\n'}, SHORT_SEND=1),
]


def harness_source(case):
    events = case['events'] or [(1, 4, '')]  # tableau C non vide
    event_code = 'static const struct event events[] = {\n' + ''.join(
        f'    {{{kind}, {fd}, {c_string(msg)}}},\n' for kind, fd, msg in events
    ) + '};'
    expected_code = 'static const char *expected[32] = {\n' + ''.join(
        f'    [{fd}] = {c_string(msg)},\n'
        for fd, msg in case['expected'].items()
    ) + '    [0] = NULL\n};'
    source = HARNESS.replace('EVENTS', event_code).replace('EXPECTED', expected_code)
    for option in ('FAIL_ALLOC', 'FAIL_CALLOC', 'FAIL_BIND', 'EXPECT_FATAL',
                   'SKIP_WRITABLE', 'SHORT_SEND'):
        source = source.replace(option, str(case['options'].get(option, 0)))
    return source




def main():
    arguments = [arg for arg in sys.argv[1:] if arg != '--valgrind']
    source = pathlib.Path(arguments[0] if arguments else pathlib.Path(__file__).resolve().parents[1] / 'mini_serv_select_simple_2.c')
    use_valgrind = '--valgrind' in sys.argv[1:]
    global HARNESS
    code = HARNESS
    code = code.replace('static char received[32][65536];',
        'static int writable[32], send_calls, recv_calls;\nstatic char received[32][65536];')
    code = code.replace('    live[3] = 1;',
        '    if (FAIL_SOCKET) return -1;\n    live[3] = 1;')
    code = code.replace('(void)fd; (void)backlog;\n    return 0;',
        '(void)fd; (void)backlog;\n    return FAIL_LISTEN ? -1 : 0;')
    code = code.replace('    if (!r)', '    if (FAIL_SELECT) return -1;\n    if (!r)')
    code = code.replace('        return 1; /* laisse au programme le temps de vider ses sorties */',
        '        for (int fd = 4; fd < 32; fd++) writable[fd] = w && FD_ISSET(fd, w);\n'
        '        return 1; /* laisse au programme le temps de vider ses sorties */')
    code = code.replace('    return 1;\n}\n\nint __wrap_accept',
        '    for (int fd = 4; fd < 32; fd++) writable[fd] = w && FD_ISSET(fd, w);\n'
        '    return 1;\n}\n\nint __wrap_accept')
    code = code.replace('    if (ev->kind == 3)',
        '    if (++recv_calls == RETRY_RECV) return -1;\n    if (ev->kind == 3)')
    code = code.replace('    if (SHORT_SEND && size > 1)',
        '    if (!writable[fd]) fail("send sans readiness en ecriture");\n'
        '    if (++send_calls == RETRY_SEND) return -1;\n'
        '    if (SHORT_SEND && size > 1)')
    code += r'''
    void __real_exit(int status) __attribute__((noreturn));
    void __wrap_exit(int status)
    {
        if (status == 1)
            for (int fd = 3; fd < 32; fd++)
                if (live[fd]) fail("fd non ferme lors de Fatal error");
        __real_exit(status);
    }
    '''
    HARNESS = code
    cases = [{**case, 'options': dict(case['options'])} for case in CASES]
    for case in cases:
        if len(case['args']) > 1:
            case.update(name='Arguments supplementaires acceptes',
                        events=CONNECT, expected={4: ARRIVED}, returncode=0, stderr=b'')
    cases += [
        scenario('Echec de socket', events=[], returncode=1, stderr=b'Fatal error\n', FAIL_SOCKET=1),
        scenario('Echec de listen', events=[], returncode=1, stderr=b'Fatal error\n', FAIL_LISTEN=1),
        scenario('Echec de select', events=[], returncode=1, stderr=b'Fatal error\n', FAIL_SELECT=1),
        scenario('Echec de calloc dans extract_message', events=CONNECT + [(2, 4, 'bonjour\n')],
                      returncode=1, stderr=b'Fatal error\n', FAIL_CALLOC=1, EXPECT_FATAL=1),
        scenario('recv negatif puis reception reussie', events=CONNECT + [(2, 4, 'bonjour\n')],
                      expected={4: ARRIVED, 5: 'client 0: bonjour\n'}, RETRY_RECV=1),
        scenario('send negatif puis envoi reussi', events=CONNECT + [(2, 4, 'bonjour\n')],
                      expected={4: ARRIVED, 5: 'client 0: bonjour\n'}, RETRY_SEND=1),
        scenario('Lecture ferme un client egalement pret en ecriture', events=CONNECT + [(3, 4, '')],
                      expected={5: 'server: client 0 just left\n'}),
        scenario('Depart avec sortie en attente puis reutilisation du fd',
                      events=CONNECT + [(3, 4, ''), (1, 4, ''), (2, 4, 'bonjour\n')],
                      expected={5: 'server: client 0 just left\nserver: client 2 just arrived\nclient 2: bonjour\n'}),
    ]
    for allocation in (2, 4, 5):
        cases.append(scenario(f'Echec allocation {allocation}',
                      events=CONNECT + [(2, 4, 'bon'), (2, 4, 'jour\n')],
                      returncode=1, stderr=b'Fatal error\n', FAIL_ALLOC=allocation, EXPECT_FATAL=1))

    failures = 0
    with tempfile.TemporaryDirectory(prefix='rank6-select-checks-') as directory:
        temp = pathlib.Path(directory)
        obj, harness, binary = temp / 'student.o', temp / 'harness.c', temp / 'server'
        subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-Dmain=student_main',
                        '-c', str(source), '-o', str(obj)], check=True)
        wraps = ['socket', 'bind', 'listen', 'accept', 'select', 'recv', 'send',
                 'close', 'malloc', 'calloc', 'realloc', 'exit']
        for case in cases:
            text = harness_source(case)
            for option in ('FAIL_SOCKET', 'FAIL_LISTEN', 'FAIL_SELECT', 'RETRY_SEND', 'RETRY_RECV'):
                text = text.replace(option, str(case['options'].get(option, 0)))
            harness.write_text(text)
            subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', str(harness), str(obj),
                            '-o', str(binary)] + ['-Wl,--wrap=' + name for name in wraps], check=True)
            command = [str(binary), *case['args']]
            if use_valgrind:
                command = ['valgrind', '--quiet', '--leak-check=full',
                           '--errors-for-leak-kinds=definite,indirect', '--error-exitcode=99', *command]
            result = subprocess.run(command, capture_output=True, timeout=10)
            success = (result.returncode == case['returncode'] and result.stderr == case['stderr']
                       and not result.stdout)
            failures += not success
            print(('OK ' if success else 'ECHEC ') + case['name'], flush=True)
            if not success: print(result.returncode, result.stderr, flush=True)
    print(f'{source.name} : {len(cases) - failures}/{len(cases)} tests reussis', flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
