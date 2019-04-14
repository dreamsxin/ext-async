<?php

namespace Concurrent;

require_once dirname(__DIR__, 3) . '/assets/functions.php';

function socketpair(): array
{
    $errno = null;
    $errstr = null;

    $server = @\stream_socket_server('async-tcp://127.0.0.1:0', $errno, $errstr, \STREAM_SERVER_BIND | \STREAM_SERVER_LISTEN);

    if ($server === false) {
        throw new \RuntimeException(\sprintf('Failed to create server: [%d] %s', $errno, $errstr));
    }

    try {
        $addr = \trim(@\stream_socket_get_name($server, false));
        $m = null;

        if (!\preg_match("':([1-9][0-9]+)$'", $addr, $m)) {
            throw new \RuntimeException('Failed to determine server port');
        }

        $t1 = Task::async(function (int $port) {
            $errno = null;
            $errstr = null;

            $socket = @\stream_socket_client('async-tcp://127.0.0.1:' . $port, $errno, $errstr, 1, \STREAM_CLIENT_CONNECT);

            if ($socket === false) {
                throw new \RuntimeException(\sprintf('Connect failed: [%d] %s', $errno, $errstr));
            }

            return $socket;
        }, (int) $m[1]);

        $t2 = Task::async(function () use ($server) {
            $socket = @\stream_socket_accept($server);

            if ($socket === false) {
                throw new \RuntimeException(\sprintf('Failed to accept socket: %s', \error_get_last()['message'] ?? ''));
            }

            return $socket;
        });
        
        return Task::await(\Concurrent\all([$t1, $t2]));
    } finally {
        @\fclose($server);
    }
}
