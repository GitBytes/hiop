#ifndef HIOP_LOGGER
#define HIOP_LOGGER

#include <cstdio>
#include <cstdarg>

class hiopVector;
class hiopResidual;
class hiopIterate;
class hiopMatrix;
class hiopHessianLowRank;
class hiopNlpFormulation;
/* Verbosity 0 to 9 */
enum hiopOutVerbosity {
  hovError=-2,
  hovWarning=-1,
  hovSummary=0, //summary of the problem and each iteration
  hovScalars=1, //additional, usually scalars, such as norm of resids, nlp and log bar errors, etc
  hovFcnEval=2, //the above plus info about the number of function, gradient and Hessians
  hovLinesearch=3, //linesearch info
  hovLinAlgScalars=4, //print out various scalars: e.g., linear systems residuals
  hovLinesearchVerb=5, //linesearch with more output
  hovLinAlgScalarsVerb=6, //additional scalars, e.g., BFGS updating info
  hovIteration=7, //print out iteration
  hovMatrices=8,
  hovMaxVerbose=9
};

class hiopLogger
{
public:
  hiopLogger(hiopNlpFormulation* nlp, hiopOutVerbosity max_desired, FILE* f) : _f(f), _nlp(nlp), _verb(max_desired) {};
  virtual ~hiopLogger() {};
  /* outputs a vector. loggerid indicates which logger should be used, by default stdout*/
  void write(const char* msg, const hiopVector& vec,          hiopOutVerbosity v, int loggerid=0);
  void write(const char* msg, const hiopResidual& r,          hiopOutVerbosity v, int loggerid=0);
  void write(const char* msg, const hiopIterate& r,           hiopOutVerbosity v, int loggerid=0);
  void write(const char* msg, const hiopMatrix& M,            hiopOutVerbosity v, int loggerid=0);
  void write(const char* msg, const hiopHessianLowRank& Hess, hiopOutVerbosity v, int loggerid=0);
  void write(const char* msg, hiopOutVerbosity v, int loggerid=0);
  //only for loggerid=0 for now
  void printf(hiopOutVerbosity v, const char* format, ...); 
  
protected:
  FILE* _f;
  char _buff[1024];
  hiopNlpFormulation* _nlp;
private:
  hiopOutVerbosity _verb;
};

#endif
