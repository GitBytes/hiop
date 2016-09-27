#include "hiopMatrix.hpp"

#include <cstdio>
#include <cstring> //for memcpy
#include <cmath>
#include <cassert>

#include "blasdefs.hpp"

#include "hiopVector.hpp"

hiopMatrixDense::hiopMatrixDense(const long long& m, const long long& glob_n, long long* col_part/*=NULL*/, MPI_Comm comm_/*=MPI_COMM_SELF*/, const long long& m_max_alloc/*=-1*/)
{
  m_local=m; n_global=glob_n;
  comm=comm_;
  int P=0;
  if(col_part) {
#ifdef WITH_MPI
    int ierr=MPI_Comm_rank(comm, &P); assert(ierr==MPI_SUCCESS);
#endif
    glob_jl=col_part[P]; glob_ju=col_part[P+1];
  } else {
    glob_jl=0; glob_ju=n_global;
  }
  n_local=glob_ju-glob_jl;
  
  max_rows=m_max_alloc;
  if(max_rows==-1) max_rows=m_local;
  assert(max_rows>=m_local && "the requested extra allocation is smaller than the allocation needed by the matrix");

  //M=new double*[m_local==0?1:m_local];
  M=new double*[max_rows==0?1:max_rows];
  M[0] = max_rows==0?NULL:new double[max_rows*n_local];
  for(int i=1; i<max_rows; i++)
    M[i]=M[0]+i*n_local;

  //internal temporary buffers to follow
}
hiopMatrixDense::~hiopMatrixDense()
{
  if(M) {
    if(M[0]) delete[] M[0];
    delete[] M;
  }
}

hiopMatrixDense::hiopMatrixDense(const hiopMatrixDense& dm)
{
  n_local=dm.n_local; m_local=dm.m_local; n_global=dm.n_global;
  glob_jl=dm.glob_jl; glob_ju=dm.glob_ju;
  comm=dm.comm;

  //M=new double*[m_local==0?1:m_local];
  max_rows = dm.max_rows;
  M=new double*[max_rows==0?1:max_rows];
  //M[0] = m_local==0?NULL:new double[m_local*n_local];
  M[0] = max_rows==0?NULL:new double[max_rows*n_local];
  //for(int i=1; i<m_local; i++)
  for(int i=1; i<max_rows; i++)
    M[i]=M[0]+i*n_local;
}

void hiopMatrixDense::appendRow(const hiopVectorPar& row)
{
#ifdef DEEP_CHECKING  
  assert(row.get_size()==n_local);
  assert(m_local<max_rows && "no more space to append rows ... should have preallocated more rows.");
#endif
  memcpy(M[m_local], row.local_data_const(), n_local*sizeof(double));
  m_local++;
}

void hiopMatrixDense::copyFrom(const hiopMatrixDense& dm)
{
  assert(n_local==dm.n_local); assert(m_local==dm.m_local); assert(n_global==dm.n_global);
  assert(glob_jl==dm.glob_jl); assert(glob_ju==dm.glob_ju);
  memcpy(M[0], dm.M[0], m_local*n_local*sizeof(double));
  //for(int i=1; i<m_local; i++)
  //  M[i]=M[0]+i*n_local;
}

void hiopMatrixDense::copyFrom(const double* buffer)
{
  memcpy(M[0], buffer, m_local*n_local*sizeof(double));
}

void  hiopMatrixDense::copyRowsFrom(const hiopMatrixDense& src, int num_rows, int row_dest)
{
#ifdef DEEP_CHECKING
  assert(row_dest>=0);
  assert(n_global==src.n_global);
  assert(n_local==src.n_local);
  assert(row_dest+num_rows<=m_local);
  assert(num_rows<=src.m_local);
#endif
  memcpy(M[row_dest], src.M[0], n_local*num_rows*sizeof(double));
}

hiopMatrixDense* hiopMatrixDense::alloc_clone() const
{
  hiopMatrixDense* c = new hiopMatrixDense(*this);
  return c;
}

hiopMatrixDense* hiopMatrixDense::new_copy() const
{
  hiopMatrixDense* c = new hiopMatrixDense(*this);
  c->copyFrom(*this);
  return c;
}

void hiopMatrixDense::setToZero()
{
  //for(int i=0; i<m_local; i++)
  //  for(int j=0; j<n_local; j++)
  //    M[i][j]=0.0;
  setToConstant(0.0);
}
void hiopMatrixDense::setToConstant(double c)
{
  double* buf=M[0]; 
  for(int j=0; j<n_local; j++) *(buf++)=c;
  
  buf=M[0]; int inc=1;
  for(int i=1; i<m_local; i++)
   dcopy_(&n_local, buf, &inc, M[i], &inc);
  
  //memcpy(M[i], buf, sizeof(double)*n_local); 
  //memcpy has similar performance as dcopy_; both faster than a loop
}

void hiopMatrixDense::print(int maxRows, int maxCols, int rank) const
{
  int myrank=0; 
#ifdef WITH_MPI
  if(rank>=0) assert(MPI_Comm_rank(comm, &myrank)==MPI_SUCCESS);
#endif
  if(myrank==rank || rank==-1) {
    if(maxRows>m_local) maxRows=m_local;
    if(maxCols>n_local) maxCols=n_local;

    printf("hiopMatrixDense::printing max=[%d,%d] (local_dims=[%d,%d], on rank=%d)\n", 
	   maxRows, maxCols, m_local,n_local,myrank);
    maxRows = maxRows>=0?maxRows:m_local;
    maxCols = maxCols>=0?maxCols:n_local;

    //printf("[[%d %d]]\n", maxRows, maxCols);
    
    for(int i=0; i<maxRows; i++) {
      for(int j=0; j<maxCols; j++) 
	printf("%22.16e ", M[i][j]);
      printf(";\n");
    }
  }
}

/* y = beta * y + alpha * this * x */
void hiopMatrixDense::timesVec(double beta, hiopVector& y_,
			       double alpha, const hiopVector& x_) const
{
  hiopVectorPar& y = dynamic_cast<hiopVectorPar&>(y_);
  const hiopVectorPar& x = dynamic_cast<const hiopVectorPar&>(x_);
#ifdef DEEP_CHECKING
  assert(y.get_local_size() == m_local);
  assert(y.get_size() == m_local); //y should not be distributed
  assert(x.get_local_size() == n_local);
  assert(x.get_size() == n_global);
#endif
  char fortranTrans='T';
  int MM=m_local, NN=n_local, incx_y=1;

  if( MM != 0 && NN != 0 ) {

#ifdef WITH_MPI
    //only add beta*y on one processor (rank 0)
    int myrank;
    int ierr=MPI_Comm_rank(comm, &myrank); assert(MPI_SUCCESS==ierr);
    if(myrank!=0) beta=0.0; 
#endif

    // the arguments seem reversed but so is trans='T' 
    // required since we keep the matrix row-wise, while the Fortran/BLAS expects them column-wise
    dgemv_( &fortranTrans, &NN, &MM, &alpha, &M[0][0], &NN,
	    x.local_data_const(), &incx_y, &beta, y.local_data(), &incx_y );
  } else {
    if( MM != 0 ) y.scale( beta );
  }
#ifdef WITH_MPI
  double* yglob=new double[m_local]; //shouldn't be any performance issue here since m_local is small
  int ierr=MPI_Allreduce(y.local_data(), yglob, m_local, MPI_DOUBLE, MPI_SUM, comm); assert(MPI_SUCCESS==ierr);
  memcpy(y.local_data(), yglob, m_local*sizeof(double));
  delete[] yglob;
#endif
  
}

/* y = beta * y + alpha * transpose(this) * x */
void hiopMatrixDense::transTimesVec(double beta, hiopVector& y_,
				    double alpha, const hiopVector& x_) const
{
  hiopVectorPar& y = dynamic_cast<hiopVectorPar&>(y_);
  const hiopVectorPar& x = dynamic_cast<const hiopVectorPar&>(x_);
#ifdef DEEP_CHECKING
  assert(x.get_local_size() == m_local);
  assert(x.get_size() == m_local); //x should not be distributed
  assert(y.get_local_size() == n_local);
  assert(y.get_size() == n_global);
#endif
  char fortranTrans='N';
  int MM=m_local, NN=n_local, incx_y=1;

  if( MM!=0 && NN!=0 ) {
    // the arguments seem reversed but so is trans='T' 
    // required since we keep the matrix row-wise, while the Fortran/BLAS expects them column-wise
    dgemv_( &fortranTrans, &NN, &MM, &alpha, &M[0][0], &NN,
	    x.local_data_const(), &incx_y, &beta, y.local_data(), &incx_y );
  } else {
    if( NN != 0 ) y.scale( beta );
  }
}

/* W = beta*W + alpha*this*X 
 * -- this is 'M' mxn, X is nxk, W is mxk
 */
void hiopMatrixDense::timesMat(double beta, hiopMatrix& W_, double alpha, const hiopMatrix& X_) const
{
#ifndef WITH_MPI
  timesMat_local(beta,W_,alpha,X_);
#else
  hiopMatrixDense& W = dynamic_cast<hiopMatrixDense&>(W_); double** WM=W.local_data();
  
  int myrank, ierr;
  ierr=MPI_Comm_rank(comm,&myrank); assert(ierr==MPI_SUCCESS);
  if(0==myrank) timesMat_local(beta,W_,alpha,X_);
  else          timesMat_local(0.,  W_,alpha,X_);

  int n2Red=W.m()*W.n(); double* Wglob=new double[n2Red]; //!opt
  ierr = MPI_Allreduce(WM[0], Wglob, n2Red, MPI_DOUBLE, MPI_SUM,comm); assert(ierr==MPI_SUCCESS);
  memcpy(WM[0], Wglob, n2Red*sizeof(double));
  delete[] Wglob;
#endif

}

/* W = beta*W + alpha*this*X 
 * -- this is 'M' mxn, X is nxk, W is mxk
 */
void hiopMatrixDense::timesMat_local(double beta, hiopMatrix& W_, double alpha, const hiopMatrix& X_) const
{
  const hiopMatrixDense& X = dynamic_cast<const hiopMatrixDense&>(X_);
  hiopMatrixDense& W = dynamic_cast<hiopMatrixDense&>(W_);
#ifdef DEEP_CHECKING  
  assert(W.m()==this->m());
  assert(X.m()==this->n());
  assert(W.n()==X.n());
#endif
  assert(W.n_local==W.n_global && "requested multiplication should be done in parallel using timesMat");
  if(W.m()==0 || X.m()==0 || W.n()==0) return;

  /* C = alpha*op(A)*op(B) + beta*C in our case is
     Wt= alpha* Xt  *Mt    + beta*Wt */
  char trans='N'; 
  int M=X.n(), N=m_local, K=X.m();
  int ldx=X.n(), ldm=n_local, ldw=X.n();

  double** XM=X.local_data(); double** WM=W.local_data();
  dgemm_(&trans,&trans, &M,&N,&K, &alpha,XM[0],&ldx, this->M[0],&ldm, &beta,WM[0],&ldw);

  /* C = alpha*op(A)*op(B) + beta*C in our case is
     Wt= alpha* Xt  *Mt    + beta*Wt */

  //char trans='T';
  //int lda=X.m(), ldb=n_local, ldc=W.n();
  //int M=X.n(), N=this->m(), K=this->n_local;

  //dgemm_(&trans,&trans, &M,&N,&K, &alpha,XM[0],&lda, this->M[0],&ldb, &beta,WM[0],&ldc);
  
}

/* W = beta*W + alpha*this^T*X 
 * -- this is mxn, X is mxk, W is nxk
 */
void hiopMatrixDense::transTimesMat(double beta, hiopMatrix& W_, double alpha, const hiopMatrix& X_) const
{
  const hiopMatrixDense& X = dynamic_cast<const hiopMatrixDense&>(X_);
  hiopMatrixDense& W = dynamic_cast<hiopMatrixDense&>(W_);
#ifdef DEEP_CHECKING
  assert(W.m()==n_local);
  assert(X.m()==m_local);
  assert(W.n()==X.n());
#endif
  if(W.m()==0) return;

  /* C = alpha*op(A)*op(B) + beta*C in our case is Wt= alpha* Xt  *M    + beta*Wt */
  char transX='N', transM='T';
  int ldx=X.n(), ldm=n_local, ldw=W.n();
  int M=X.n(), N=n_local, K=X.m();
  double** XM=X.local_data(); double** WM=W.local_data();
  
  dgemm_(&transX, &transM, &M,&N,&K, &alpha,XM[0],&ldx, this->M[0],&ldm, &beta,WM[0],&ldw);
}

/* W = beta*W + alpha*this*X^T
 * -- this is mxn, X is kxn, W is mxk
 */
void hiopMatrixDense::timesMatTrans_local(double beta, hiopMatrix& W_, double alpha, const hiopMatrix& X_) const
{
  const hiopMatrixDense& X = dynamic_cast<const hiopMatrixDense&>(X_);
  hiopMatrixDense& W = dynamic_cast<hiopMatrixDense&>(W_);
#ifdef DEEP_CHECKING
  assert(W.m()==m_local);
  assert(X.n()==n_local);
  assert(W.n()==X.m());
#endif
  assert(W.n_local==W.n_global && "not intended for multiplication in parallel");
  if(W.m()==0) return;
  

  /* C = alpha*op(A)*op(B) + beta*C in our case is Wt= alpha* X  *Mt    + beta*Wt */
  char transX='T', transM='N';
  int ldx=X.n(), ldm=n_local, ldw=W.n();
  int M=X.m(), N=m_local, K=n_local;
  double** XM=X.local_data(); double** WM=W.local_data();
  
  dgemm_(&transX, &transM, &M,&N,&K, &alpha,XM[0],&ldx, this->M[0],&ldm, &beta,WM[0],&ldw);
}
void hiopMatrixDense::timesMatTrans(double beta, hiopMatrix& W_, double alpha, const hiopMatrix& X_) const
{
#ifdef DEEP_CHECKING
  const hiopMatrixDense& X = dynamic_cast<const hiopMatrixDense&>(X_);
  hiopMatrixDense& W = dynamic_cast<hiopMatrixDense&>(W_);
  assert(W.n_local==W.n_global && "not intended for parallel cases");
#endif
  timesMatTrans_local(beta,W_,alpha,X_);
}
void hiopMatrixDense::addDiagonal(const hiopVector& d_)
{
  const hiopVectorPar& d = dynamic_cast<const hiopVectorPar&>(d_);
#ifdef DEEP_CHECKING
  assert(d.get_size()==n());
  assert(d.get_size()==m());
  assert(d.get_local_size()==m_local);
  assert(d.get_local_size()==n_local);
#endif
  const double* dd=d.local_data_const();
  for(int i=0; i<n_local; i++) M[i][i] += dd[i];
}

void hiopMatrixDense::addSubDiagonal(long long start, const hiopVector& d_)
{
  const hiopVectorPar& d = dynamic_cast<const hiopVectorPar&>(d_);
  long long dlen=d.get_size();
#ifdef DEEP_CHECKING
  assert(start>=0);
  assert(start+dlen<=n_local);
#endif

  const double* dd=d.local_data_const();
  for(int i=start; i<start+dlen; i++) M[i][i] += dd[i-start];
}

void hiopMatrixDense::addMatrix(double alpha, const hiopMatrix& X_)
{
  const hiopMatrixDense& X = dynamic_cast<const hiopMatrixDense&>(X_); 
#ifdef DEEP_CHECKING
  assert(m_local==X.m_local);
  assert(n_local==X.n_local);
#endif
  //  extern "C" void   daxpy_(int* n, double* da, double* dx, int* incx, double* dy, int* incy );
  int N=m_local*n_local, inc=1;
  daxpy_(&N, &alpha, X.M[0], &inc, M[0], &inc);
}

double hiopMatrixDense::max_abs_value()
{
  char norm='M';
  double maxv = dlange_(&norm, &n_local, &m_local, M[0], &n_local, NULL);
#ifdef WITH_MPI
  double maxvg;
  int ierr=MPI_Allreduce(&maxv,&maxvg,1,MPI_DOUBLE,MPI_MAX,comm); assert(ierr==MPI_SUCCESS);
  return maxvg;
#endif
  return maxv;
}

#ifdef DEEP_CHECKING
bool hiopMatrixDense::assertSymmetry(double tol) const
{
  //must be square
  if(m_local!=n_global) assert(false);

  //symmetry
  for(int i=0; i<n_local; i++)
    for(int j=0; j<n_local; j++)
      if(fabs(M[i][j]-M[j][i])/(1+M[i][j]) > tol) assert(false);
  return true;
}

#endif