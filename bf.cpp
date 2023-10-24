#include <cassert>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

class Config
{
    constexpr static int GB = 1000 * 1000 * 1000;
    constexpr static int MB = 1000 * 1000;
    constexpr static int KB = 1000;
    // constexpr static int BLKSIZE = 8 * GB;
    constexpr static int BLKSIZE = 1 * KB;

  public:
    int p, blk, tot, buf;
    Config(int p, int mode)
        : p(p), blk(BLKSIZE / p / (p + 2)), tot((p - 1) * p * blk),
          buf(p * (p + 2) * blk)
    {
    }
};

class File
{
  public:
    constexpr static int MAXFNUM = 4000;
    int size, blk_num, index;
    char filename[32];
};

File files[File::MAXFNUM];
int fcnt = 0;

void make_work_dirs(int p);
void load_meta(int p);
void save_meta(int p);

int mod(int x, int p);
void blk_xor(char *dst, char *src, size_t len);

void evenodd_write(int p, const char *ipt, const char *fname);

void evenodd_read(int p, const char *fname, const char *opt);
void evenodd_read0(int p, File &f, const char *opt);
void evenodd_read1(int p, File &f, const char *opt, int fail_disk);
void evenodd_read2(int p, File &f, const char *opt, int fail0, int fail1);
void evenodd_read2_pfail(int p, File &f, const char *opt, int fail_disk);

void evenodd_repair(int p, int idx0, int idx1);

int main(int argc, const char *argv[])
{
    assert(argc == 5 || argc == 4);
    const char *op = argv[1], *ipt = nullptr, *opt = nullptr, *fname = nullptr;
    int p = atoi(argv[2]), idx0 = 0, idx1 = 0;
    make_work_dirs(p);
    load_meta(p);
    switch (op[2]) {
    case 'i': // write
        ipt = argv[3];
        fname = argv[4];
        evenodd_write(p, ipt, fname);
        break;
    case 'a': // read
        fname = argv[3];
        opt = argv[4];
        evenodd_read(p, fname, opt);
        break;
    case 'p': // repair
        throw "not implemented";
        idx0 = atoi(argv[3]);
        idx1 = argc == 4 ? -1 : atoi(argv[4]);
        // evenodd_repair(p, idx0, idx1);
        break;
    default:
        return -1;
    }
    return 0;
}

void make_work_dirs(int p)
{
    for (int i = 0; i <= p + 1; i++) {
        std::filesystem::create_directory(std::format("disk_{}", i));
    }
}

void load_meta(int p)
{
    for (int i = 0; i <= p + 1; i++) {
        std::string fname = std::format("disk_{}/meta", i);
        if (std::filesystem::exists(fname)) {
            std::ifstream fin(fname, std::ios::binary);
            fin.read((char *)&files, sizeof(files));
            fin.close();
            for (; fcnt < File::MAXFNUM && files[fcnt].size; fcnt++)
                ;
            return;
        }
    }
}

std::string internal_name(int fidx, int blk_idx)
{
    return std::format("{:04x}_{:02x}", fidx, blk_idx).c_str();
}

void save_meta(int p)
{
    for (int i = 0; i <= p + 1; i++) {
        std::string fname = std::format("disk_{}/meta", i);
        std::ofstream fout(fname, std::ios::binary);
        fout.write((char *)&files, sizeof(files));
        fout.close();
    }
}

int mod(int x, int p) { return (x % p + p) % p; }

void blk_xor(char *dst, char *src, size_t len)
{
    for (int i = 0; i < len; i++) {
        dst[i] ^= src[i];
    }
}

void evenodd_write(int p, const char *ipt, const char *fname)
{
    Config cfg(p, 0);
    char blk[p + 2][p][cfg.blk];

    std::ifstream fin(ipt, std::ios::binary);

    File &f = files[fcnt++];
    strcpy(f.filename, fname);
    f.size = std::filesystem::file_size(ipt);
    f.blk_num = (f.size + cfg.tot - 1) / cfg.tot;
    f.index = fcnt - 1;

    char S[cfg.blk];

    for (int blk_id = 0; blk_id < f.blk_num; blk_id++) {
        memset(blk, 0x00, sizeof(blk));
        for (int t = 0; t < p; t++) {
            fin.read(blk[t][0], cfg.blk * (p - 1));
        }

        for (int l = 0; l < p - 1; ++l) {
            for (int t = 0; t < p; ++t) {
                blk_xor(blk[p][l], blk[t][l], cfg.blk);
            }
        }

        memset(S, 0x00, cfg.blk);
        for (int t = 1; t < p; ++t) {
            blk_xor(S, blk[t][p - 1 - t], cfg.blk);
        }

        for (int l = 0; l < p - 1; ++l) {
            for (int t = 0; t < p; ++t) {
                blk_xor(blk[p + 1][l], blk[t][mod(l - t, p)], cfg.blk);
            }
        }
        for (int l = 0; l < p - 1; ++l) {
            blk_xor(blk[p + 1][l], S, cfg.blk);
        }

        for (int t = 0; t <= p+1; t++) {
            std::ofstream fout(
                std::format("disk_{}/{}", t, internal_name(fcnt - 1, blk_id)),
                std::ios::binary);
            fout.write(blk[t][0], cfg.blk * (p - 1));
        }
    }
    save_meta(p);
}

// read f with no disk failed
void evenodd_read0(int p, File &f, const char *opt)
{
    Config cfg(p, 0);
    char blk[1][p][cfg.blk]; // only need space for one disk
    memset(blk, 0x00, sizeof(blk));

    std::ofstream fout(opt, std::ios::binary);
    int remain = f.size;

    for (int blk_id = 0; blk_id < f.blk_num; blk_id++) {
        for (int t = 0; t < p; t++) {
            std::ifstream fin(
                std::format("disk_{}/{}", t, internal_name(f.index, blk_id)),
                std::ios::binary);
            fin.read(blk[0][0], std::min(cfg.blk * (p - 1), remain));

            fout.write(blk[0][0], std::min(cfg.blk * (p - 1), remain));
            // std::cout.write(blk[0][0], std::min(cfg.blk * (p - 1), remain));
            remain -= cfg.blk * (p - 1);
            if (remain <= 0) {
                break;
            }
        }
    }
}

// read f with fail_disk-th disk failed
void evenodd_read1(int p, File &f, const char *opt, int fail_disk)
{
    if (fail_disk >= p) {
        evenodd_read0(p, f, opt);
        return;
    }
    Config cfg(p, 0);
    char blk[p+2][p][cfg.blk];
    memset(blk, 0x00, sizeof(blk));

    std::ofstream fout(opt, std::ios::binary);
    int remain = f.size;
    for (int blk_id = 0; blk_id < f.blk_num; blk_id++) {
        for (int t = 0; t <= p; t++) // ok data disks & horizontal parity disk
        {
            if (t == fail_disk) {
                memset(blk[t][0], 0x00, cfg.blk * (p - 1));
                continue;
            }
            std::ifstream fin(
                std::format("disk_{}/{}", t, internal_name(f.index, blk_id)),
                std::ios::binary);
            fin.read(blk[t][0], cfg.blk * (p - 1));
        }

        for (int t = 0; t <= p; t++) {
            if (t == fail_disk) {
                continue;
            }
            blk_xor(blk[fail_disk][0], blk[t][0], cfg.blk * (p - 1));
        }

        // write out
        for (int t = 0; t < p; t++) {
            fout.write(blk[t][0], std::min(cfg.blk * (p - 1), remain));
            remain -= cfg.blk * (p - 1);
            if (remain <= 0) {
                break;
            }
        }
        fout.flush();
    }
}

// read f with fail_disk-th and p-th disk failed
void evenodd_read2_pfail(int p, File &f, const char *opt, int fail_disk)
{
    Config cfg(p, 0);
    char blk[p+2][p][cfg.blk];
    memset(blk, 0x00, sizeof(blk));

    int i = fail_disk, i_1 = mod(fail_disk-1, p); // the i-1-th diagonal does not cross the failed disk
    char S[cfg.blk];
    std::ofstream fout(opt, std::ios::binary);
    int remain = f.size;
    for(int blk_id = 0; blk_id < f.blk_num; blk_id++) {
        for(int t = 0; t <= p+1; t++) {
            if(t == i || t == p) {
                memset(blk[t][0], 0x00, cfg.blk * (p - 1));
                continue;
            }
            std::ifstream fin(
                std::format("disk_{}/{}", t, internal_name(f.index, blk_id)),
                std::ios::binary);
            fin.read(blk[t][0], cfg.blk * (p - 1));
        }

        memcpy(S, blk[p+1][i_1], cfg.blk); // maybe can just ref blk[p+1][i_1]
        for(int l = 0; l < p; ++l) {
            blk_xor(S, blk[l][mod(i_1 - l, p)], cfg.blk);
        }

        for(int k=0; k <= p-2; ++k) {
            memcpy(blk[i][k], S, cfg.blk);
            blk_xor(blk[i][k], blk[p+1][mod(i+k, p)], cfg.blk);
            for(int l=0; l < p; ++l) {
                if(l == i) {
                    continue;
                }
                blk_xor(blk[i][k], blk[l][mod(i+k-l, p)], cfg.blk);
            }
        }

        // write out
        for (int t = 0; t < p; t++) {
            fout.write(blk[t][0], std::min(cfg.blk * (p - 1), remain));
            remain -= cfg.blk * (p - 1);
            if (remain <= 0) {
                break;
            }
        }
    }
}

// read f with fail0-th and fail1-th disk failed
void evenodd_read2(int p, File &f, const char *opt, int fail0, int fail1)
{
    assert(fail0 < fail1);
    if(fail0 >= p) {
        evenodd_read0(p, f, opt);
        return;
    }
    if(fail1 == p+1) {
        evenodd_read1(p, f, opt, fail0);
        return;
    }
    if(fail1 == p) {
        evenodd_read2_pfail(p, f, opt, fail0);
        return;
    }

    // fail0 < fail1 < p, two data disks fail
    Config cfg(p, 0);
    char blk[p+2][p][cfg.blk];
    memset(blk, 0x00, sizeof(blk));
    
    char S[cfg.blk];
    char S0[p][cfg.blk], S1[p][cfg.blk]; // TODO: on heap?

    int i = fail0, j = fail1;
    std::ofstream fout(opt, std::ios::binary);
    int remain = f.size;
    for(int blk_id=0; blk_id < f.blk_num; ++blk_id) {
        for(int t=0; t <= p+1; ++t) {
            if(t == i || t == j) {
                memset(blk[t][0], 0x00, cfg.blk * (p - 1));
                continue;
            }
            std::ifstream fin(
                std::format("disk_{}/{}", t, internal_name(f.index, blk_id)),
                std::ios::binary);
            fin.read(blk[t][0], cfg.blk * (p - 1));
        }
        
        memset(S, 0x00, cfg.blk);
        memset(S0, 0x00, cfg.blk * p);

        // calc S
        for(int l=0; l<=p-2; ++l) {
            blk_xor(S, blk[p][l], cfg.blk);
        }
        for(int l=0; l<=p-2; ++l) {
            blk_xor(S, blk[p+1][l], cfg.blk);
        }

        // calc S0
        for(int u=0; u<p; ++u) {
            for(int l=0; l<=p; ++l) {
                if(l == i || l == j) {
                    continue;
                }
                blk_xor(S, blk[l][u], cfg.blk);
            }
        }

        // calc S1
        for(int u=0; u<p; ++u) {
            memcpy(S1[u], S, cfg.blk);
        }
        for(int u=0; u<p; ++u) {
            blk_xor(S1[u], blk[p+1][u], cfg.blk);
        }
        for(int u=0; u<p; ++u) {
            for(int l=0; l<p; ++l) {
                if(l == i || l == j) {
                    continue;
                }
                blk_xor(S1[u], blk[l][mod(u-l, p)], cfg.blk);
            }
        }

        int s = mod(i - j - 1, p);
        for(; s != p-1; s = mod(s + i - j, p)) {
            memcpy(blk[j][s], S1[mod(j + s, p)], cfg.blk);
            blk_xor(blk[j][s], blk[i][mod(s + j - i, p)], cfg.blk);

            memcpy(blk[i][s], S0[s], cfg.blk);
            blk_xor(blk[i][s], blk[j][s], cfg.blk);
        }

        // write out
        for (int t = 0; t < p; t++) {
            fout.write(blk[t][0], std::min(cfg.blk * (p - 1), remain));
            remain -= cfg.blk * (p - 1);
            if (remain <= 0) {
                break;
            }
        }
    }
}

void evenodd_read(int p, const char *fname, const char *opt)
{
    int fidx = -1;
    for (int i = 0; i < fcnt; i++) {
        if (strcmp(files[i].filename, fname) == 0) {
            fidx = i;
            break;
        }
    }
    assert(fidx != -1);
    File &f = files[fidx];
    std::ofstream fout(opt, std::ios::binary);

    std::vector<int> fail_disk;
    for (int d = 0; d < p + 2; d++) {
        if (!std::filesystem::exists(
                std::format("disk_{}/{}", d, internal_name(fidx, 0)))) {
            fail_disk.push_back(d);
        }
    }
    assert(fail_disk.size() <= 2);

    switch (fail_disk.size()) {
    case 0:
        evenodd_read0(p, f, opt);
        break;
    case 1:
        evenodd_read1(p, f, opt, fail_disk[0]);
        break;
    case 2:
        evenodd_read2(p, f, opt, fail_disk[0], fail_disk[1]);
        break;
    default:
        break;
    }
}

void evenodd_repair(int p, int idx0, int idx1) {}