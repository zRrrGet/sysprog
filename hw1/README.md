python3 generator.py -f test1.txt -c 10000 -m 10000

python3 generator.py -f test2.txt -c 10000 -m 10000

python3 generator.py -f test3.txt -c 10000 -m 10000

python3 generator.py -f test4.txt -c 10000 -m 10000

python3 generator.py -f test5.txt -c 10000 -m 10000

python3 generator.py -f test6.txt -c 100000 -m 10000

gcc main.c libcoro.c -o ./main

default number of coroutines is number of files provided

default number of target latency is 0(yield at each iteration)

./main test1.txt test2.txt test3.txt test4.txt test5.txt test6.txt

python3 checker.py -f result

-c Coroutine number

-l Target latency in ms

./main -c 3 -l 10 test1.txt test2.txt test3.txt test4.txt test5.txt test6.txt