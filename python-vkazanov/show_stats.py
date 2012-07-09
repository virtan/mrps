import sys
import hotshot, hotshot.stats
stats = hotshot.stats.load(sys.argv[1])
stats.strip_dirs()
stats.sort_stats('time', 'calls')
stats.print_stats(20)
