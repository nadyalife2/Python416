let alertWrapper = document.querySelector('.alert');
let alertClose = document.querySelectorAll('.alert__close');

if (alertWrapper) {
  for(let i=0; i < alertClose.length; i++){
    alertClose[i].addEventListener('click', function(){
      this.parentNode.remove();
    })
  }
}