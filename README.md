<img src="src/Moruga.png" style="width:200px" draggable="false" unselectable="on"/>


# Moruga
High Performance Data Compression

Using: **Moruga &lt;option&gt; c &lt;infile&gt; &lt;outfile&gt;**

| File | Original | Compressed | Ratio | Option | Memory used | Time used | Speed |
|:-----|:--------:|:----------:|:-----:|:------:|:-----------:|:---------:|:-----:|
| enwik8 | 100000000 | 18935794 | 18% | -0 | 73 MiB | 188.2 s | 1882 ns/sec |
| enwik8 | 100000000 | 18566070 | 18% | -1 | 101 MiB | 192.5 s | 1925 ns/sec |
| enwik8 | 100000000 | 18200762 | 18% | -2 | 157 MiB | 194.3 s | 1943 ns/sec |
| enwik8 | 100000000 | 17888851 | 17% | -3 | 269 MiB | 194.6 s | 1946 ns/sec |
| enwik8 | 100000000 | 17676683 | 17% | -4 | 494 MiB | 197.7 s | 1977 ns/sec |
| enwik8 | 100000000 | 17559203 | 17% | -5 | 943 MiB | 201.9 s | 2019 ns/sec |
| enwik8 | 100000000 | 17486266 | 17% | -6 | 1840 MiB | 209.1 s | 2091 ns/sec |
| enwik8 | 100000000 | 17477140 | 17% | -7 | 3636 MiB | 214.3 s | 2143 ns/sec |
| enwik8 | 100000000 | 17474766 | 17% | -8 | 7227 MiB | 213.7 s | 2137 ns/sec |
| enwik8 | 100000000 | 17474154 | 17% | -9 | 13383 MiB | 216.4 s | 2164 ns/sec |
| enwik9 | 1000000000 | 157965499 | 15% | -0 | 144 MiB | 1805.2 s | 1805 ns/sec |
| enwik9 | 1000000000 | 154628346 | 15% | -1 | 144 MiB | 1850.5 s | 1850 ns/sec |
| enwik9 | 1000000000 | 151059208 | 15% | -2 | 157 MiB | 1865.2 s | 1865 ns/sec |
| enwik9 | 1000000000 | 147209419 | 14% | -3 | 270 MiB | 1882.9 s | 1883 ns/sec |
| enwik9 | 1000000000 | 143652866 | 14% | -4 | 494 MiB | 1919.0 s | 1919 ns/sec |
| enwik9 | 1000000000 | 140702879 | 14% | -5 | 943 MiB | 1958.2 s | 1958 ns/sec |
| enwik9 | 1000000000 | 138365990 | 13% | -6 | 1841 MiB | 2007.0 s | 2007 ns/sec |
| enwik9 | 1000000000 | 137121001 | 13% | -7 | 3636 MiB | 2068.5 s | 2068 ns/sec |
| enwik9 | 1000000000 | 136598319 | 13% | -8 | 7227 MiB | 2084.1 s | 2084 ns/sec |
| enwik9 | 1000000000 | 136281285 | 13% | -9 | 13383 MiB | 2123.4 s | 2123 ns/sec |


Using: **bzip2 --best &lt;infile&gt;**

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| enwik8 | 100000000 | 29008758 | 29%
| enwik9 | 1000000000 | 253977891 | 25%


Using: **gzip --best &lt;infile&gt;**

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| enwik8 | 100000000 | 36445248 | 36%
| enwik9 | 1000000000 | 322591995 | 32%


Using: **xz --lzma2=preset=9e,dict=1GiB,lc=4,pb=0 &lt;infile&gt;**

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| enwik8 | 100000000 | 24703772 | 24%
| enwik9 | 1000000000 | 197331816 | 19%

Using: **Moruga &lt;option&gt; c &lt;infile&gt; &lt;outfile&gt;**

| File | Original | Compressed | Ratio | Option | Memory used | Time used | Speed |
|:-----|:--------:|:----------:|:-----:|:------:|:-----------:|:---------:|:-----:|
| silesia/dickens | 10192446 | 2061055 | 20% | -6 | 1840 MiB | 21.5 s | 2109 ns/sec |
| silesia/mozilla | 51220480 | 10251437 | 20% | -6 | 1857 MiB | 194.1 s | 3789 ns/sec |
| silesia/mr | 9970564 | 2107003 | 21% | -6 | 1840 MiB | 23.0 s | 2305 ns/sec |
| silesia/nci | 33553445 | 970334 | 2% | -6 | 1841 MiB | 23.2 s | 691 ns/sec |
| silesia/ooffice | 6152192 | 1737908 | 28% | -6 | 1840 MiB | 23.5 s | 3819 ns/sec |
| silesia/osdb | 10085684 | 2153464 | 21% | -6 | 1840 MiB | 38.9 s | 3858 ns/sec |
| silesia/reymont | 6627202 | 862772 | 13% | -6 | 1857 MiB | 21.4 s | 3225 ns/sec |
| silesia/samba | 21606400 | 2243645 | 10% | -6 | 1856 MiB | 79.3 s | 3668 ns/sec |
| silesia/sao | 7251944 | 4362806 | 60% | -6 | 1840 MiB | 36.3 s | 5002 ns/sec |
| silesia/webster | 41458703 | 5453463 | 13% | -6 | 1841 MiB | 90.7 s | 2188 ns/sec |
| silesia/x-ray | 8474240 | 3629719 | 42% | -6 | 1840 MiB | 30.3 s | 3577 ns/sec |
| silesia/xml | 5345280 | 307182 | 5% | -6 | 1841 MiB | 9.0 s | 1681 ns/sec |


Using: **bzip2 --best &lt;infile&gt;**

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| silesia/dickens | 10192446 | 2799520 | 27%
| silesia/mozilla | 51220480 | 17914392 | 34%
| silesia/mr | 9970564 | 2441280 | 24%
| silesia/nci | 33553445 | 1812734 | 5%
| silesia/ooffice | 6152192 | 2862526 | 46%
| silesia/osdb | 10085684 | 2802792 | 27%
| silesia/reymont | 6627202 | 1246230 | 18%
| silesia/samba | 21606400 | 4549759 | 21%
| silesia/sao | 7251944 | 4940524 | 68%
| silesia/webster | 41458703 | 8644714 | 20%
| silesia/x-ray | 8474240 | 4051112 | 47%
| silesia/xml | 5345280 | 441186 | 8%


Using: **gzip --best &lt;infile&gt;**

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| silesia/dickens | 10192446 | 3851823 | 37%
| silesia/mozilla | 51220480 | 18994142 | 37%
| silesia/mr | 9970564 | 3673940 | 36%
| silesia/nci | 33553445 | 2987533 | 8%
| silesia/ooffice | 6152192 | 3090442 | 50%
| silesia/osdb | 10085684 | 3716342 | 36%
| silesia/reymont | 6627202 | 1820834 | 27%
| silesia/samba | 21606400 | 5408272 | 25%
| silesia/sao | 7251944 | 5327041 | 73%
| silesia/webster | 41458703 | 12061624 | 29%
| silesia/x-ray | 8474240 | 6037713 | 71%
| silesia/xml | 5345280 | 662284 | 12%


Using: **xz --lzma2=preset=9e,dict=1GiB,lc=4,pb=0 &lt;infile&gt;**

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| silesia/dickens | 10192446 | 2825584 | 27%
| silesia/mozilla | 51220480 | 13672236 | 26%
| silesia/mr | 9970564 | 2758392 | 27%
| silesia/nci | 33553445 | 1440120 | 4%
| silesia/ooffice | 6152192 | 2421724 | 39%
| silesia/osdb | 10085684 | 2861260 | 28%
| silesia/reymont | 6627202 | 1313860 | 19%
| silesia/samba | 21606400 | 3728036 | 17%
| silesia/sao | 7251944 | 4638136 | 63%
| silesia/webster | 41458703 | 8358852 | 20%
| silesia/x-ray | 8474240 | 4508396 | 53%
| silesia/xml | 5345280 | 433876 | 8%
