#!/usr/bin/perl -w

use strict;

use Scalar::Util 'refaddr';
use Time::HiRes 'sleep';
use POSIX qw(fcntl F_SETFL F_GETFL O_NONBLOCK);
use Socket ':all';
use IO::Socket::INET;


sub PORT()		{ 9090 }
sub NTHREADS()		{ 24 }
sub MAXCONNECT()	{ 430 }		# slightly more than 10k/24
sub BUFSIZE()		{ 4096 }
sub DELAY_MIN()		{ 0.001 }
sub DELAY_RND()		{ 0.002 }

#sub INFO { print @_ }
sub INFO { }

my @pids;

$|=1;
my $srv = IO::Socket::INET->new(Listen => 128, LocalPort=>PORT, ReuseAddr=>1, Blocking=>0) || die "main: $!";

for (1 .. NTHREADS) {
	my $p = fork();
	if (!$p) {
		Work();
		exit;
	}
	push @pids, $p;
}

$srv->close();
$srv = undef;

$SIG{INT} = sub { INFO "main: kill\n"; kill 15, @pids; };

while (wait() != -1) {}

INFO "main: exit\n";


sub Work
{
	my $nConnects = MAXCONNECT;
	my $delay = DELAY_MIN + rand(DELAY_RND);
	my @s;
	my $buf = " " x BUFSIZE;
	my $sz;
	my $n;

	INFO "$$: start\n";
	$SIG{INT} = sub { $_->close() for @s; @s=(); INFO "$$: exit\n"; exit; };

	while (1) {
		sleep $delay;
		if ($nConnects) {
			if (my $s = $srv->accept()) {
				INFO "$$: accept $nConnects = ".refaddr($s)."\n";
				if (!--$nConnects) { close $srv; $srv = undef; }
				$s->autoflush(1);
				setsockopt($s, IPPROTO_TCP, TCP_NODELAY, 1) or die "$$: setsockopt: $!";
				fcntl($s, F_SETFL, fcntl($s, F_GETFL, 0) | O_NONBLOCK) or die "$$: setsockopt: $!";
				# ioctl($s, FIONBIO, 1) or die "$$: setsockopt: $!";
				push @s, $s;
			}
		}
		$n = 0;
		for (@s) {
			$sz = sysread($_, $buf,BUFSIZE);
			if (!defined($sz)) {
			} elsif ($sz > 0) {
				$_->syswrite($buf);
			} else {
				INFO "$$: close ".refaddr($_)."\n";
				$_->close();
				splice @s, $n, 1;
			}
			$n++;
		}
	}
}

