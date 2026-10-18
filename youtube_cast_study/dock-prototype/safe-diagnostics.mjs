// Dependency errors can carry Lounge tokens, URLs and personal sender data.
// Keep only recognized error types, transport codes and numeric HTTP statuses.
const names=new Set(['Error','TypeError','SessionError','ConnectionError','AbortError','TimeoutError',
  'BadResponseError','DataError','IncompleteAPIDataError','AppError','DialServerError','SenderConnectionError']);
const codes=new Set(['ECONNRESET','ECONNREFUSED','ETIMEDOUT','ENOTFOUND','EAI_AGAIN','EPIPE',
  'UND_ERR_SOCKET','UND_ERR_HEADERS_TIMEOUT','UND_ERR_BODY_TIMEOUT','CERT_HAS_EXPIRED',
  'UNABLE_TO_VERIFY_LEAF_SIGNATURE','SELF_SIGNED_CERT_IN_CHAIN']);
export function diagnostic(error) {
  const result=[];const seen=new Set();
  for(let current=error;current && !seen.has(current) && result.length<6;current=current.cause) {
    seen.add(current);const item={type:names.has(current.name)?current.name:'Error'};
    if(codes.has(current.code))item.code=current.code;
    if(Array.isArray(current.info?.missing))item.missing=current.info.missing.filter(x=>
      ['SID','gsessionid','loungeIdToken','screenId','pairingCode'].includes(x));
    const status=current.info?.response?.status;
    if(Number.isInteger(status)&&status>=100&&status<=599)item.httpStatus=status;
    result.push(item);
  }
  return result;
}
export const safeLogger={
  setLevel(){},debug(){},info(){},warn(){},
  error(...args){for(const error of args)if(error instanceof Error)
    console.error('CAST_SESSION_DIAGNOSTIC',JSON.stringify(diagnostic(error)));}
};
