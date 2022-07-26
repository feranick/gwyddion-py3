#!/usr/bin/env perl
# $Id: invert_perl.pl 22454 2019-08-31 19:53:40Z yeti-dn $
# A very simple Gwyddion plug-in example in Perl.
# Written by Yeti <yeti@gwyddion.net>.  Public domain.
use warnings;
use strict;

my $errmsg = "Plug-in has to be called from Gwyddion plugin-proxy.";
push @INC, $ENV{ 'GWYPLUGINLIB' } . '/perl' if exists $ENV{ 'GWYPLUGINLIB' };
{
    local $SIG{ '__DIE__' } = sub { die "$errmsg\n" };
    require "Gwyddion/dump.pm";
}

# Plug-in information.
my %run_modes = ( 'noninteractive' => 1, 'with_defaults' => 1 );

my $plugin_info =
"invert_perl
/_Test/Value Invert (Perl)
" . join( ' ', keys %run_modes ) . "\n";

sub register {
    print $plugin_info;
}

sub run {
    my ( $run_mode, $filename ) = @_;
    my ( $data, $dfield, $n, $a, $min, $max, $mirror );

    $data = Gwyddion::dump::read( $filename );
    $dfield = $data->{ '/0/data' };
    $a = $dfield->{ 'data' };
    $n = scalar @$a;

    $min = 1e38;
    $max = -1e38;
    for my $v ( @$a ) {
       $min = $v if $v < $min;
       $max = $v if $v > $max;
    }
    $mirror = $min + $max;
    for my $i ( 0 .. $n-1 ) { $a->[$i] = $mirror - $a->[$i] }
    Gwyddion::dump::write( $data, $filename );
}

my %functions = ( 'register' => \&register, 'run' => \&run );
my $what = shift @ARGV;
if ( not $what or not exists $functions{ $what } ) { die "$errmsg\n" }

$functions{ $what }->( @ARGV );
