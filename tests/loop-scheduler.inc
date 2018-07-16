<?php

/*
 +----------------------------------------------------------------------+
 | PHP Version 7                                                        |
 +----------------------------------------------------------------------+
 | Copyright (c) 1997-2018 The PHP Group                                |
 +----------------------------------------------------------------------+
 | This source file is subject to version 3.01 of the PHP license,      |
 | that is bundled with this package in the file LICENSE, and is        |
 | available through the world-wide-web at the following url:           |
 | http://www.php.net/license/3_01.txt                                  |
 | If you did not receive a copy of the PHP license and are unable to   |
 | obtain it through the world-wide-web, please send a note to          |
 | license@php.net so we can mail you a copy immediately.               |
 +----------------------------------------------------------------------+
 | Authors: Martin Schröder <m.schroeder2007@gmail.com>                 |
 +----------------------------------------------------------------------+
 */

namespace Concurrent;

class TimerLoop
{
    protected $ticks = [];
    
    protected $timers;
    
    public function __construct()
    {
        $this->timers = new \SplPriorityQueue();
    }
    
    public function nextTick(callable $callback)
    {
        $this->ticks[] = $callback;
    }
    
    public function timer(int $delay, callable $callback)
    {
        $due = \microtime(true) + ($delay / 1000);
        
        $this->timers->insert([
            $due,
            $callback
        ], -1 * $due);
    }
    
    public function run()
    {
        while ($this->ticks || !$this->timers->isEmpty()) {
            $ticks = $this->ticks;
            $this->ticks = [];
            
            foreach ($ticks as $callback) {
                $callback();
            }
            
            if ($this->timers->isEmpty()) {
                continue;
            }
            
            $now = \microtime(true);
            $sleep = null;
            
            do {
                list ($due, $callback) = $this->timers->top();
                
                if ($due > $now) {
                    $sleep = ($due - $now) * 1000000;
                    
                    break;
                }
                
                $this->timers->extract();
                
                $callback();
            } while (!$this->timers->isEmpty());
            
            if ($sleep !== null) {
                \usleep($sleep);
            }
        }
    }
}

class TimerLoopScheduler extends LoopTaskScheduler
{
    private $loop;
    
    public function __construct(TimerLoop $loop)
    {
        $this->loop = $loop;
    }
    
    protected function activate()
    {
        $this->loop->nextTick(\Closure::fromCallable([
            $this,
            'dispatch'
        ]));
    }
    
    protected function runLoop()
    {
        $this->loop->run();
    }
}
