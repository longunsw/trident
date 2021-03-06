#include <trident/ml/batch.h>
#include <trident/kb/kb.h>
#include <trident/kb/querier.h>

#include <fstream>
#include <algorithm>
#include <random>

BatchCreator::BatchCreator(string kbdir, uint64_t batchsize,
        uint16_t nthreads,
        const float valid,
        const float test,
        const bool filter,
        const bool createBatchFile,
        std::shared_ptr<Feedback> feedback) : kbdir(kbdir), batchsize(batchsize),
    /*nthreads(nthreads),*/ valid(valid), test(test),
    createBatchFile(createBatchFile), filter(filter),
    feedback(feedback) {
        rawtriples = NULL;
        ntriples = 0;
        currentidx = 0;
    }

std::shared_ptr<Feedback> BatchCreator::getFeedback() {
    return feedback;
}

string BatchCreator::getValidPath() {
    return kbdir + "/_batch_valid";
}

string BatchCreator::getTestPath() {
    return kbdir + "/_batch_test";
}

string BatchCreator::getValidPath(string kbdir) {
    return kbdir + "/_batch_valid";
}

string BatchCreator::getTestPath(string kbdir) {
    return kbdir + "/_batch_test";
}

void BatchCreator::createInputForBatch(bool createTraining,
        const float valid, const float test) {
    KBConfig config;
    KB kb(kbdir.c_str(), true, false, false, config);
    Querier *q = kb.query();
    auto itr = q->get(IDX_POS, -1, -1, -1);
    int64_t s,p,o;

    ofstream ofs_valid;
    if (valid > 0) {
        ofs_valid.open(this->kbdir + "/_batch_valid", ios::out | ios::app | ios::binary);
    }
    ofstream ofs_test;
    if (test > 0) {
        ofs_test.open(this->kbdir + "/_batch_test", ios::out | ios::app | ios::binary);
    }
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0, 1.0);

    //Create a file called '_batch' in the maindir with a fixed-length record size
    ofstream ofs;
    if (createTraining) {
        LOG(INFOL) << "Store the input for the batch process in " << kbdir + "/_batch ...";
        ofs.open(this->kbdir + "/_batch", ios::out | ios::app | ios::binary);
    }
    while (itr->hasNext()) {
        itr->next();
        p = itr->getKey();
        o = itr->getValue1();
        s = itr->getValue2();
        const char *cs = (const char*)&s;
        const char *cp = (const char*)&p;
        const char *co = (const char*)&o;
        if (valid > 0 && dis(gen) < valid) {
            ofs_valid.write(cs, 5); //Max numbers have 5 bytes
            ofs_valid.write(cp, 5);
            ofs_valid.write(co, 5);
        } else if (test > 0 && dis(gen) < test) {
            ofs_test.write(cs, 5); //Max numbers have 5 bytes
            ofs_test.write(cp, 5);
            ofs_test.write(co, 5);
        } else if (createTraining) {
            ofs.write(cs, 5); //Max numbers have 5 bytes
            ofs.write(cp, 5);
            ofs.write(co, 5);
        }
    }
    q->releaseItr(itr);

    if (createTraining) {
        ofs.close();
    }
    if (valid > 0) {
        ofs_valid.close();
    }
    if (test > 0) {
        ofs_test.close();
    }

    delete q;
    LOG(INFOL) << "Done";
}

struct _pso {
    const char *rawtriples;
    _pso(const char *rawtriples) : rawtriples(rawtriples) {}
    bool operator() (const uint64_t idx1, const uint64_t idx2) const {
        int64_t s1 = *(int64_t*)(rawtriples + idx1 * 15);
        s1 = s1 & 0xFFFFFFFFFFl;
        int64_t p1 = *(int64_t*)(rawtriples + idx1 * 15 + 5);
        p1 = p1 & 0xFFFFFFFFFFl;
        int64_t o1 = *(int64_t*)(rawtriples + idx1 * 15 + 10);
        o1 = o1 & 0xFFFFFFFFFFl;
        int64_t s2 = *(int64_t*)(rawtriples + idx2 * 15);
        s2 = s2 & 0xFFFFFFFFFFl;
        int64_t p2 = *(int64_t*)(rawtriples + idx2 * 15 + 5);
        p2 = p2 & 0xFFFFFFFFFFl;
        int64_t o2 = *(int64_t*)(rawtriples + idx2 * 15 + 10);
        o2 = o2 & 0xFFFFFFFFFFl;
        if (p1 != p2) {
            return p1 < p2;
        } else if (s1 != s2) {
            return s1 < s2;
        } else {
            return o1 < o2;
        }
    }
};

void BatchCreator::start() {
    //First check if the file exists
    string fin = this->kbdir + "/_batch";
    if (Utils::exists(fin)) {
    } else {
        if (createBatchFile) {
            LOG(INFOL) << "Could not find the input file for the batch. I will create it and store it in a file called '_batch'";
            createInputForBatch(createBatchFile, valid, test);
        }
    }

    if (createBatchFile) {
        //Load the file into a memory-mapped file
        this->mappedFile = std::unique_ptr<MemoryMappedFile>(new MemoryMappedFile(
                    fin));
        this->rawtriples = this->mappedFile->getData();
        this->ntriples = this->mappedFile->getLength() / 15;
    } else {
        this->kbbatch = std::unique_ptr<KBBatch>(new KBBatch(kbdir));
        this->kbbatch->populateCoordinates();
        this->ntriples = this->kbbatch->ntriples();
    }

    LOG(DEBUGL) << "Creating index array ...";
    this->indices.resize(this->ntriples);
    for(int64_t i = 0; i < this->ntriples; ++i) {
        this->indices[i] = i;
    }

    LOG(DEBUGL) << "Shuffling array ...";
    std::shuffle(this->indices.begin(), this->indices.end(), engine);
    this->currentidx = 0;
    LOG(DEBUGL) << "Done";
}

bool BatchCreator::getBatch(std::vector<uint64_t> &output) {
    int64_t i = 0;
    output.resize(this->batchsize * 3);
    //The output vector is already supposed to contain batchsize elements. Otherwise, resize it
    while (i < batchsize && currentidx < ntriples) {
        int64_t idx = indices[currentidx];
        uint64_t s,p,o;
        if (createBatchFile) {
            s = *(uint64_t*)(rawtriples + idx * 15);
            s = s & 0xFFFFFFFFFFl;
            p = *(uint64_t*)(rawtriples + idx * 15 + 5);
            p = p & 0xFFFFFFFFFFl;
            o = *(uint64_t*)(rawtriples + idx * 15 + 10);
            o = o & 0xFFFFFFFFFFl;
        } else {
            kbbatch->getAt(idx, s, p, o);
        }
        if (filter && shouldBeUsed(s,p,o)) {
            output[i*3] = s;
            output[i*3+1] = p;
            output[i*3+2] = o;
            i+=1;
        }
        currentidx++;
    }
    if (i < batchsize) {
        output.resize(i*3);
    }
    return i > 0;
}

bool BatchCreator::shouldBeUsed(int64_t s, int64_t p, int64_t o) {
    return feedback->shouldBeIncluded(s, p, o);
}

bool BatchCreator::getBatch(std::vector<uint64_t> &output1,
        std::vector<uint64_t> &output2,
        std::vector<uint64_t> &output3) {
    int64_t i = 0;
    output1.resize(this->batchsize);
    output2.resize(this->batchsize);
    output3.resize(this->batchsize);
    //The output vector is already supposed to contain batchsize elements.
    //Otherwise, resize it
    while (i < batchsize && currentidx < ntriples) {
        int64_t idx = indices[currentidx];
        uint64_t s,p,o;
        if (createBatchFile) {
            s = *(uint64_t *)(rawtriples + idx * 15);
            s = s & 0xFFFFFFFFFFl;
            p = *(uint64_t *)(rawtriples + idx * 15 + 5);
            p = p & 0xFFFFFFFFFFl;
            o = *(uint64_t *)(rawtriples + idx * 15 + 10);
            o = o & 0xFFFFFFFFFFl;
        } else {
            kbbatch->getAt(idx, s, p, o);
        }
        if (!filter || shouldBeUsed(s,p,o)) {
            output1[i] = s;
            output2[i] = p;
            output3[i] = o;
            i+=1;
        }
        currentidx++;
    }
    if (i < batchsize) {
        output1.resize(i);
        output2.resize(i);
        output3.resize(i);
    }
    return i > 0;
}

void BatchCreator::loadTriples(string path, std::vector<uint64_t> &output) {
    MemoryMappedFile mf(path);
    char *start = mf.getData();
    char *end = start + mf.getLength();
    while (start < end) {
        int64_t s = *(int64_t*)(start);
        s = s & 0xFFFFFFFFFFl;
        int64_t p = *(int64_t*)(start + 5);
        p = p & 0xFFFFFFFFFFl;
        int64_t o = *(int64_t*)(start + 10);
        o = o & 0xFFFFFFFFFFl;
        output.push_back(s);
        output.push_back(p);
        output.push_back(o);
        start += 15;
    }
}

BatchCreator::KBBatch::KBBatch(string kbdir) {
    KBConfig config;
    this->kb = std::unique_ptr<KB>(new KB(kbdir.c_str(), true, false, false, config));
    this->querier = std::unique_ptr<Querier>(kb->query());
}

void BatchCreator::KBBatch::populateCoordinates() {
    //Load all files
    allposfiles = kb->openAllFiles(IDX_POS);
    auto itr = (TermItr*)querier->getTermList(IDX_POS);
    string kbdir = kb->getPath();
    string posdir = kbdir + "/p" + to_string(IDX_POS);
    //Get all the predicates
    uint64_t current = 0;
    while (itr->hasNext()) {
        itr->next();
        uint64_t pred = itr->getKey();
        uint64_t card = itr->getCount();
        PredCoordinates info;
        info.pred = pred;
        info.boundary = current + card;

        //Get beginning of the table
        short currentFile = itr->getCurrentFile();
        int64_t currentMark = itr->getCurrentMark();
        string fdidx = posdir + "/" + to_string(currentFile) + ".idx";
        if (Utils::exists(fdidx)) {
            ifstream idxfile(fdidx);
            idxfile.seekg(8 + 11 * currentMark);
            char buffer[5];
            idxfile.read(buffer, 5);
            int64_t pos = Utils::decode_longFixedBytes(buffer, 5);
            info.buffer = allposfiles[currentFile] + pos;
        } else {
            LOG(ERRORL) << "Cannot happen!";
            throw 10;
        }
        char currentStrat = itr->getCurrentStrat();
        int storageType = StorageStrat::getStorageType(currentStrat);
        if (storageType == NEWCOLUMN_ITR) {
            //Get reader and offset
            char header1 = info.buffer[0];
            char header2 = info.buffer[1];
            const uint8_t bytesPerStartingPoint =  header2 & 7;
            const uint8_t bytesPerCount = (header2 >> 3) & 7;
            const uint8_t remBytes = bytesPerCount + bytesPerStartingPoint;
            const uint8_t bytesPerFirstEntry = (header1 >> 3) & 7;
            const uint8_t bytesPerSecondEntry = (header1) & 7;
            info.offset = remBytes;

            //update the buffer
            int offset = 2;
            info.nfirstterms = Utils::decode_vlong2(info.buffer, &offset);
            Utils::decode_vlong2(info.buffer, &offset);
            info.buffer = info.buffer + offset;

            FactoryNewColumnTable::get12Reader(bytesPerFirstEntry,
                    bytesPerSecondEntry, &info.reader);
        } else if (storageType == NEWROW_ITR) {
            const char nbytes1 = (currentStrat >> 3) & 3;
            const char nbytes2 = (currentStrat >> 1) & 3;
            FactoryNewRowTable::get12Reader(nbytes1, nbytes2, &info.reader);
        } else {
            LOG(ERRORL) << "Not supported";
            throw 10;
        }
        predicates.push_back(info);
        current += card;
    }
    querier->releaseItr(itr);
}

void BatchCreator::KBBatch::getAt(uint64_t pos,
        uint64_t &s,
        uint64_t &p,
        uint64_t &o) {
    auto itr = predicates.begin();
    uint64_t offset = 0;
    while(itr != predicates.end() && pos >= itr->boundary) {
        offset = itr->boundary;
        itr++;
    }
    pos -= offset;
    if (itr != predicates.end()) {
        //Take the reader and read the values
        itr->reader(itr->nfirstterms, itr->offset,
                itr->buffer, pos, o, s);
        p = itr->pred;
    }
}

BatchCreator::KBBatch::~KBBatch() {
    querier = NULL;
    kb = NULL;
}

uint64_t BatchCreator::KBBatch::ntriples() {
    return kb->getSize();
}
